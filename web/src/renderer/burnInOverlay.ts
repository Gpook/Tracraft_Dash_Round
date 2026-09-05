/**
 * burnInOverlay.ts
 * Два визуальных инструмента для редактора:
 *
 * 1. showOrbit(ctx, display) — рисует 9 позиций орбиты pixel shift
 *    поверх канвы когда включён режим "Show Burn-in Info".
 *
 * 2. getBurnInRisk(widget) — оценивает риск выгорания виджета (0–1).
 *    Используется для окраски рамки в инспекторе.
 *
 * Эти функции чисто визуальные, в прошивку не идут.
 */

import { Widget } from '@/schema/layout'

// Орбита — те же 9 позиций что в C++ ORBIT[]
const ORBIT: [number, number][] = [
    [ 0,  0],
    [ 2,  0],
    [ 1,  2],
    [-1,  2],
    [-2,  0],
    [-2, -1],
    [ 0, -2],
    [ 2, -1],
    [ 1,  1],
]

/**
 * Рисует индикатор орбиты pixel shift в левом нижнем углу канвы.
 * @param phase текущая позиция орбиты (0–8), анимируется по времени
 */
export function drawOrbitIndicator(
    ctx: CanvasRenderingContext2D,
    _displayW: number,
    displayH: number,
    phase: number,
) {
    const cx = 40, cy = displayH - 40
    const scale = 6  // 1 px орбиты = 6 px на индикаторе

    ctx.save()

    // Фон индикатора
    ctx.fillStyle = 'rgba(0,0,0,0.55)'
    ctx.beginPath()
    ctx.arc(cx, cy, 26, 0, Math.PI * 2)
    ctx.fill()
    ctx.strokeStyle = 'rgba(255,255,255,0.12)'
    ctx.lineWidth = 1
    ctx.stroke()

    // Все позиции — серые точки
    for (const [ox, oy] of ORBIT) {
        ctx.fillStyle = 'rgba(255,255,255,0.20)'
        ctx.beginPath()
        ctx.arc(cx + ox * scale, cy + oy * scale, 2, 0, Math.PI * 2)
        ctx.fill()
    }

    // Текущая позиция — яркая точка
    const [px, py] = ORBIT[phase % ORBIT.length]
    ctx.fillStyle = '#0A84FF'
    ctx.shadowColor = '#0A84FF'
    ctx.shadowBlur = 6
    ctx.beginPath()
    ctx.arc(cx + px * scale, cy + py * scale, 3.5, 0, Math.PI * 2)
    ctx.fill()
    ctx.shadowBlur = 0

    // Подпись
    ctx.font = '9px Inter, sans-serif'
    ctx.fillStyle = 'rgba(255,255,255,0.35)'
    ctx.textAlign = 'center'
    ctx.fillText('SHIFT', cx, cy + 22)

    ctx.restore()
}

/**
 * Оценка риска выгорания виджета (0 = нет риска, 1 = высокий).
 *
 * Учитывает:
 * - Тип виджета (label и image — статичные → выше риск)
 * - Цвет (белый/светлый → выше)
 * - Размер (большой статичный элемент → выше)
 */
export function getBurnInRisk(widget: Widget): number {
    let risk = 0

    // Статичные типы
    if (widget.type === 'label' || widget.type === 'image') risk += 0.5
    else if (widget.type === 'warning') risk += 0.2  // иконки мигают — немного
    else risk += 0.05  // динамичные виджеты почти не горят

    // Цвет (для label, warning)
    const color = ('props' in widget && (widget.props as any).color as string | undefined)
        ?? ('props' in widget && (widget.props as any).trackColor as string | undefined)
    if (color) {
        const r = parseInt(color.slice(1, 3), 16) / 255
        const g = parseInt(color.slice(3, 5), 16) / 255
        const b = parseInt(color.slice(5, 7), 16) / 255
        const luminance = 0.299 * r + 0.587 * g + 0.114 * b
        risk += luminance * 0.4  // яркий белый → +0.4
    }

    // Размер (процент от площади дисплея 466²)
    const area = widget.rect.w * widget.rect.h
    const fraction = area / (466 * 466)
    risk += fraction * 0.3

    return Math.min(1, risk)
}

/**
 * Цвет рамки риска в инспекторе.
 * low → прозрачно, medium → жёлтый, high → красный
 */
export function riskColor(risk: number): string | null {
    if (risk < 0.25) return null
    if (risk < 0.55) return '#FFCC00'
    return '#FF3B30'
}
