import React, { useRef, useState } from 'react'
import { useEditorStore } from '@/store'
import { LayoutTemplates } from './LayoutTemplates'

export function Toolbar() {
  const {
    layout, activeScreenIdx,
    playing, togglePlaying,
    addScreen, removeScreen,
    setActiveScreen, renameScreen, reorderScreens,
    undo, redo,
    exportJSON, importJSON,
    setSimEnabled,
    deviceMode, toggleDeviceMode,
    historyIdx, history,
  } = useEditorStore()

  const simEnabled = layout.sim?.enabled ?? false

  const fileInputRef = useRef<HTMLInputElement>(null)
  const [showTemplates, setShowTemplates] = useState(false)
  const [editingIdx, setEditingIdx] = useState<number | null>(null)
  const [editValue, setEditValue] = useState('')
  const dragFromRef = useRef<number | null>(null)
  const [dragOver, setDragOver] = useState<number | null>(null)

  function handleExport() {
    const json = exportJSON()
    const blob = new Blob([json], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url; a.download = `${layout.id}.json`; a.click()
    URL.revokeObjectURL(url)
  }

  function handleImport(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0]; if (!file) return
    const reader = new FileReader()
    reader.onload = (ev) => importJSON(ev.target?.result as string)
    reader.readAsText(file)
    e.target.value = ''
  }

  function commitRename(idx: number) {
    const name = editValue.trim()
    if (name) renameScreen(idx, name)
    setEditingIdx(null)
  }

  return (
    <header className="toolbar">
      {/* Левый блок */}
      <div className="toolbar-left">
        <button className="tb-btn" onClick={undo} disabled={historyIdx === 0} title="Undo (Ctrl+Z)">↩</button>
        <button className="tb-btn" onClick={redo} disabled={historyIdx === history.length - 1} title="Redo (Ctrl+Y)">↪</button>
        <div className="tb-divider" />
        <button className="tb-btn" onClick={togglePlaying} title={playing ? 'Pause preview' : 'Resume preview'}>
          {playing ? '⏸' : '▶'}
        </button>
      </div>

      {/* Центр: вкладки экранов */}
      <div className="toolbar-screens">
        {layout.screens.map((scr, idx) => (
          <div
            key={scr.id}
            className={`screen-tab ${idx === activeScreenIdx ? 'active' : ''} ${dragOver === idx ? 'drag-over' : ''}`}
            draggable
            onDragStart={() => { dragFromRef.current = idx }}
            onDragOver={(e) => { e.preventDefault(); setDragOver(idx) }}
            onDragLeave={() => setDragOver(null)}
            onDrop={(e) => {
              e.preventDefault()
              setDragOver(null)
              if (dragFromRef.current !== null && dragFromRef.current !== idx) {
                reorderScreens(dragFromRef.current, idx)
                setActiveScreen(idx)
              }
              dragFromRef.current = null
            }}
            onDragEnd={() => { dragFromRef.current = null; setDragOver(null) }}
            onClick={() => { if (editingIdx !== idx) setActiveScreen(idx) }}
            onDoubleClick={() => { setEditingIdx(idx); setEditValue(scr.name ?? `Screen ${idx + 1}`) }}
          >
            {editingIdx === idx ? (
              <input
                className="screen-tab-rename"
                value={editValue}
                onChange={e => setEditValue(e.target.value)}
                onBlur={() => commitRename(idx)}
                onKeyDown={e => {
                  if (e.key === 'Enter') commitRename(idx)
                  if (e.key === 'Escape') setEditingIdx(null)
                }}
                autoFocus
                onClick={e => e.stopPropagation()}
              />
            ) : (
              <span className="screen-tab-name">{scr.name ?? `Screen ${idx + 1}`}</span>
            )}
            {layout.screens.length > 1 && (
              <span
                className="screen-tab-close"
                onClick={e => { e.stopPropagation(); removeScreen(idx) }}
              >×</span>
            )}
          </div>
        ))}
        <button className="screen-tab-add" onClick={addScreen} title="Add screen">+</button>
      </div>

      {/* Правый блок */}
      <div className="toolbar-right">
        <button className="tb-btn" onClick={() => setShowTemplates(true)} title="Apply layout template">⊞ Templates</button>
        <div className="tb-divider" />
        <label
          className="tb-check"
          title="Записать в JSON флаг sim.enabled — устройство будет крутить симуляцию сигналов вместо CAN"
        >
          <input
            type="checkbox"
            checked={simEnabled}
            onChange={e => setSimEnabled(e.target.checked)}
          />
          Sim
        </label>
        <label
          className="tb-check"
          title="Превью с ограничениями устройства: гарнитура и кегли те же, что в прошивке. Сглаживания на AMOLED всё равно нет — этого canvas повторить не может."
        >
          <input
            type="checkbox"
            checked={deviceMode}
            onChange={toggleDeviceMode}
          />
          Как на устройстве
        </label>
        <div className="tb-divider" />
        <button className="tb-btn" onClick={() => fileInputRef.current?.click()} title="Import JSON">📂</button>
        <button className="tb-btn" onClick={handleExport} title="Export JSON">💾</button>
        <input ref={fileInputRef} type="file" accept=".json" style={{ display: 'none' }} onChange={handleImport} />
      </div>

      {showTemplates && <LayoutTemplates onClose={() => setShowTemplates(false)} />}
    </header>
  )
}
