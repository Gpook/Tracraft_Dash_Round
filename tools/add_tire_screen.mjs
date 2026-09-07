/**
 * Добавляет экран с виджетом температуры шин в лейаут.
 *
 * Разовый скрипт: нужен, чтобы новый виджет можно было проверить на устройстве
 * не пересобирая лейаут руками в редакторе. Если экран с таким id уже есть,
 * скрипт ничего не делает — повторный запуск безопасен.
 *
 *   node tools/add_tire_screen.mjs data/layouts/demo_sim.json
 */

import { readFileSync, writeFileSync } from 'node:fs'

const path = process.argv[2]
if (!path) {
  console.error('Использование: node tools/add_tire_screen.mjs <layout.json>')
  process.exit(1)
}

const SCREEN_ID = 'screen-tyres'

const layout = JSON.parse(readFileSync(path, 'utf8'))

if (layout.screens.some(s => s.id === SCREEN_ID)) {
  console.log(`Экран ${SCREEN_ID} уже есть — ничего не меняю`)
  process.exit(0)
}

layout.screens.push({
  id: SCREEN_ID,
  name: 'Tyres',
  widgets: [
    {
      id: 'label-tyres',
      type: 'label',
      rect: { x: 133, y: 44, w: 200, h: 26 },
      z: 0,
      signal: '',
      unit: '',
      props: {
        text: 'TYRE TEMP',
        align: 'center',
        fontSize: 20,
        color: '#6E6E73',
      },
    },
    {
      id: 'tiretemp-1',
      type: 'tire_temp',
      // 2x2 сетка: при 200x300 и зазорах 18/14 одна шина выходит 91x117 —
      // вертикальный прямоугольник, как и задумано
      rect: { x: 133, y: 84, w: 200, h: 300 },
      z: 1,
      signal: '',
      unit: '',
      props: {
        prefix: 'tire',
        min: 40,
        max: 110,
        showValue: true,
        showLabel: true,
        gapX: 18,
        gapY: 14,
        radius: 4,
        sectorGap: 1,
        trackColor: '#1C1C1E',
      },
    },
  ],
})

writeFileSync(path, JSON.stringify(layout, null, 1) + '\n', 'utf8')
console.log(`Экран «Tyres» добавлен в ${path} (всего экранов: ${layout.screens.length})`)
