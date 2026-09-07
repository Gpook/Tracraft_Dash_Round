/**
 * LayersPanel — слои активного экрана в порядке z.
 *
 * Список идёт сверху вниз от большего z к меньшему, то есть первым стоит то,
 * что на экране лежит поверх остального. Тот же порядок, что у панели слоёв в
 * графических редакторах, и обратный порядку отрисовки.
 */

import { Widget } from '@/schema/layout'
import { useEditorStore } from '@/store'

/// Иконки те же, что в палитре: элемент должен узнаваться между панелями.
function iconOf(w: Widget): string {
  switch (w.type) {
    case 'numeric':   return '🔢'
    case 'label':     return 'T'
    case 'bar':       return '▬'
    case 'graph':     return '📈'
    case 'gforce':    return '⊕'
    case 'steering':  return '⇄'
    case 'tire_temp': return '🌡'
    case 'clock':     return '🕐'
    case 'lap_timer': return '⏱'
    case 'warning':   return '⚠'
    case 'shift_light':
      return w.props.mode === 'arc' ? '◜●◝'
           : w.props.mode === 'flash' ? '⚡'
           : '●●●'
    default:          return '◻'
  }
}

/// Заголовок слоя. Тип сам по себе неинформативен, когда на экране четыре
/// numeric, поэтому показываем то, что отличает элемент друг от друга.
function titleOf(w: Widget): string {
  switch (w.type) {
    case 'label':       return w.props.text || 'Label'
    case 'shift_light': return `Shift ${w.props.mode ?? 'segments'}`
    case 'warning':     return w.props.label || 'Warning'
    case 'graph':       return 'Graph'
    case 'gforce':      return 'G-Force'
    case 'tire_temp':   return 'Tire Temp'
    case 'clock':       return 'Clock'
    case 'lap_timer':   return 'Lap Timer'
    default:            return w.signal ?? w.type
  }
}

export function LayersPanel() {
  const { layout, activeScreenIdx, selectedWidgetId, selectWidget, removeWidget, moveWidgetZ } =
    useEditorStore()

  const screen = layout.screens[activeScreenIdx]
  const widgets = screen?.widgets ?? []

  // Тот же порядок, по которому рисует канва (и прошивка), только развёрнутый:
  // при равных z решает позиция в массиве, поэтому сортировка обязана быть
  // устойчивой.
  const ordered = widgets
    .map((w, i) => ({ w, i }))
    .sort((a, b) => (a.w.z ?? 0) - (b.w.z ?? 0) || a.i - b.i)
    .reverse()

  return (
    <div className="layers">
      <div className="panel-title">
        <span>Layers</span>
        <span className="layers-count">{widgets.length}</span>
      </div>

      {ordered.length === 0 ? (
        <div className="layers-empty">На экране нет элементов</div>
      ) : (
        <div className="layers-list">
          {ordered.map(({ w }, row) => (
            <div
              key={w.id}
              className={`layer-row ${w.id === selectedWidgetId ? 'selected' : ''}`}
              onClick={() => selectWidget(w.id)}
              title={`${w.type} · ${w.id}`}
            >
              <span className="layer-icon">{iconOf(w)}</span>
              <span className="layer-title">{titleOf(w)}</span>
              <span className="layer-z">z{w.z ?? 0}</span>
              <span className="layer-actions">
                <button
                  className="layer-btn"
                  disabled={row === 0}
                  onClick={e => { e.stopPropagation(); moveWidgetZ(activeScreenIdx, w.id, +1) }}
                  title="Выше"
                >▲</button>
                <button
                  className="layer-btn"
                  disabled={row === ordered.length - 1}
                  onClick={e => { e.stopPropagation(); moveWidgetZ(activeScreenIdx, w.id, -1) }}
                  title="Ниже"
                >▼</button>
                <button
                  className="layer-btn danger"
                  onClick={e => { e.stopPropagation(); removeWidget(activeScreenIdx, w.id) }}
                  title="Удалить"
                >✕</button>
              </span>
            </div>
          ))}
        </div>
      )}
    </div>
  )
}
