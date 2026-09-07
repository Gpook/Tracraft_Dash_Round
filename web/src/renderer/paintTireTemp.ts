/**
 * TireTemp — температура шин по секторам.
 *
 * Четыре шины стоят по местам колёс, как вид на машину сверху. Каждая — это
 * вертикальный прямоугольник, поделённый на 4 вертикальных сектора по ширине
 * протектора, и каждый сектор красится своей температурой.
 *
 * Правый борт зеркалится: сектор с ВНУТРЕННИМ плечом всегда смотрит к центру
 * виджета. Иначе пришлось бы держать в голове, что у левой шины внутреннее
 * плечо справа, а у правой слева, и картинка перестала бы читаться с одного
 * взгляда — а в ней весь смысл.
 *
 * Соотношение плеч — то, ради чего это и смотрят: если внутренние секторы
 * горячее наружных даже в поворотах, развала слишком много.
 *
 * Сигналы собираются из префикса: `${prefix}.${угол}.t${сектор}`,
 * например tire.fl.t1 … tire.rr.t4. Сектор 1 — наружное плечо, 4 — внутреннее.
 */

import { TireTempWidget } from '@/schema/layout'
import { SignalMap } from './paintWidget'

const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v))

/// Углы в порядке отрисовки: перед сверху, зад снизу.
/// mirror — рисовать секторы справа налево, чтобы внутреннее плечо было внутри.
const CORNERS = [
  { key: 'fl', label: 'FL', col: 0, row: 0, mirror: false },
  { key: 'fr', label: 'FR', col: 1, row: 0, mirror: true  },
  { key: 'rl', label: 'RL', col: 0, row: 1, mirror: false },
  { key: 'rr', label: 'RR', col: 1, row: 1, mirror: true  },
] as const

/**
 * Шкала синий → красный через голубой, зелёный, жёлтый, оранжевый.
 *
 * Зелёный посажен в середину диапазона намеренно: это рабочая температура, и
 * «всё зелёное» должно читаться как «всё в порядке». Синее — резина не
 * прогрелась, красное — перегрев.
 */
const SCALE: { p: number; c: [number, number, number] }[] = [
  { p: 0.00, c: [ 10,  60, 200] },   // синий
  { p: 0.22, c: [  0, 190, 235] },   // голубой
  { p: 0.44, c: [ 20, 205,  90] },   // зелёный
  { p: 0.64, c: [235, 210,  20] },   // жёлтый
  { p: 0.82, c: [250, 140,  15] },   // оранжевый
  { p: 1.00, c: [240,  45,  40] },   // красный
]

/// Цвет по нормированной температуре 0..1.
export function tireColor(n01: number): string {
  const p = clamp(n01, 0, 1)

  let i = 0
  while (i < SCALE.length - 2 && p > SCALE[i + 1].p) ++i

  const a = SCALE[i], b = SCALE[i + 1]
  const t = (p - a.p) / (b.p - a.p)

  const r = Math.round(a.c[0] + (b.c[0] - a.c[0]) * t)
  const g = Math.round(a.c[1] + (b.c[1] - a.c[1]) * t)
  const bl = Math.round(a.c[2] + (b.c[2] - a.c[2]) * t)
  return `rgb(${r},${g},${bl})`
}

export function paintTireTemp(
  ctx: CanvasRenderingContext2D,
  w: TireTempWidget,
  signals: SignalMap,
  theme: { fg: string; muted: string },
) {
  const { rect, props } = w
  const {
    prefix = 'tire',
    min = 40,
    max = 110,
    showValue = true,
    showLabel = false,
    gapX = 18,
    gapY = 14,
    radius = 4,
    sectorGap = 1,
    trackColor = '#1C1C1E',
  } = props

  const span = max - min || 1

  // ─── Раскладка 2×2 ─────────────────────────────────────────────────────────
  const tileW = (rect.w - gapX) / 2
  const tileH = (rect.h - gapY) / 2
  if (tileW < 6 || tileH < 10) return

  const textH  = showValue ? Math.min(15, tileH * 0.26) : 0
  const labelH = showLabel ? Math.min(11, tileH * 0.20) : 0
  const tireH  = Math.max(6, tileH - textH - labelH)

  ctx.save()
  ctx.translate(rect.x, rect.y)

  for (const c of CORNERS) {
    const ox = c.col * (tileW + gapX)
    const oy = c.row * (tileH + gapY)

    // Подпись угла над шиной
    if (showLabel) {
      ctx.font = `${Math.max(8, Math.round(labelH * 0.85))}px Inter, sans-serif`
      ctx.fillStyle = theme.muted
      ctx.textAlign = 'center'
      ctx.textBaseline = 'top'
      ctx.fillText(c.label, ox + tileW / 2, oy)
    }

    const ty = oy + labelH
    const r  = Math.min(radius, tileW / 2, tireH / 2)

    // ─── Фон шины ───────────────────────────────────────────────────────────
    // Рисуется под секторами и служит зазорами между ними: секторы кладутся
    // с отступом, и дорожка проступает тонкими линиями-разделителями.
    ctx.fillStyle = trackColor
    ctx.beginPath()
    ctx.roundRect(ox, ty, tileW, tireH, r)
    ctx.fill()

    // ─── Секторы ────────────────────────────────────────────────────────────
    // Клип по скруглённой рамке: иначе прямоугольные секторы торчали бы за
    // срезанные углы шины.
    ctx.save()
    ctx.beginPath()
    ctx.roundRect(ox, ty, tileW, tireH, r)
    ctx.clip()

    const secW = (tileW - sectorGap * 3) / 4
    let sum = 0
    let seen = 0

    for (let i = 0; i < 4; ++i) {
      // Зеркалим правый борт: внутреннее плечо смотрит к центру виджета
      const idx = c.mirror ? 3 - i : i
      const v = signals[`${prefix}.${c.key}.t${idx + 1}`]

      const sx = ox + i * (secW + sectorGap)

      if (v === undefined) {
        // Нет сигнала — оставляем дорожку, чтобы отсутствие данных было видно
        continue
      }
      sum += v
      ++seen

      ctx.fillStyle = tireColor((v - min) / span)
      ctx.fillRect(sx, ty, secW, tireH)
    }

    ctx.restore()

    // ─── Среднее по колесу ──────────────────────────────────────────────────
    if (showValue) {
      // Среднее считается по тем секторам, что реально пришли: при частично
      // подключённых датчиках цифра остаётся осмысленной, а не падает вдвое.
      const avg = seen > 0 ? sum / seen : undefined
      ctx.font = `${Math.max(9, Math.round(textH * 0.82))}px Inter, sans-serif`
      ctx.fillStyle = avg === undefined ? theme.muted : theme.fg
      ctx.textAlign = 'center'
      ctx.textBaseline = 'bottom'
      ctx.fillText(
        avg === undefined ? '—' : `${Math.round(avg)}°`,
        ox + tileW / 2,
        ty + tireH + textH,
      )
    }
  }

  ctx.restore()
}
