import React, { useState } from 'react'
import { Toolbar } from '@/components/Toolbar'
import { WidgetPalette } from '@/components/WidgetPalette'
import { DisplayCanvas } from '@/components/DisplayCanvas'
import { PropertiesPanel } from '@/components/PropertiesPanel'
import { useEditorStore } from '@/store'

const ZOOM = 0.82

export default function App() {
  const { layout } = useEditorStore()
  const [showOrbit, setShowOrbit] = useState(false)

  // Глобальный хоткей Ctrl+Z/Y
  React.useEffect(() => {
    const { undo, redo } = useEditorStore.getState()
    const handler = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'z' && !e.shiftKey) { e.preventDefault(); undo() }
      if ((e.ctrlKey || e.metaKey) && (e.key === 'y' || (e.key === 'z' && e.shiftKey))) { e.preventDefault(); redo() }
    }
    window.addEventListener('keydown', handler)
    return () => window.removeEventListener('keydown', handler)
  }, [])

  return (
    <div className="app">
      <Toolbar />
      <div className="editor-body">
        <WidgetPalette />
        <main className="canvas-area">
          <div className="canvas-wrapper">
            <DisplayCanvas display={layout.display} zoom={ZOOM} showBurnInOrbit={showOrbit} />
          </div>
          <div className="canvas-footer">
            <span className="canvas-info">
              {layout.display.w}×{layout.display.h} · CO5300 AMOLED 1.75"
            </span>
            <button
              className={`orbit-toggle ${showOrbit ? 'active' : ''}`}
              onClick={() => setShowOrbit(v => !v)}
              title="Show pixel shift orbit preview"
            >
              ⊙ Pixel Shift
            </button>
          </div>
        </main>
        <PropertiesPanel />
      </div>
    </div>
  )
}
