/**
 * Подбор размера шрифта под бокс.
 *
 * Нужен и для Numeric, и для Label: оба должны расти вместе с рамкой,
 * но при этом не вылезать за неё по ширине (длинное число в узком виджете).
 */

import { EM_LADDER_BOLD, isDeviceMode } from './deviceFont'

/**
 * Возвращает размер шрифта (px), при котором текст влезает и по высоте, и по ширине.
 *
 * Ширина текста растёт практически линейно от размера шрифта, поэтому одного
 * замера достаточно: меряем на «высотном» размере и, если не влезли по ширине,
 * масштабируем пропорционально.
 *
 * @param fontTemplate  функция px → строка для ctx.font (семейство/жирность задаёт вызывающий)
 */

export function fitFontSize(
  ctx: CanvasRenderingContext2D,
  text: string,
  maxW: number,
  maxH: number,
  fontTemplate: (px: number) => string,
  minPx = 6,
): number {
  if (!text) return Math.max(minPx, Math.floor(maxH))

  let size = Math.max(minPx, Math.floor(maxH))
  ctx.font = fontTemplate(size)
  const measured = ctx.measureText(text).width

  if (measured > maxW && measured > 0) {
    size = Math.floor(size * (maxW / measured))
  }

  // В режиме устройства подогнанный размер тоже обязан лежать на лестнице
  // достижимых кеглей — иначе автоподгонка снова разойдётся с прошивкой.
  // Прижимаем вниз: подняться нельзя, текст вылезет из рамки.
  if (isDeviceMode()) {
    const ladder = EM_LADDER_BOLD.filter(v => v <= size)
    size = ladder.length ? ladder[ladder.length - 1] : EM_LADDER_BOLD[0]
  }

  return Math.max(minPx, size)
}
