export type PositionCoordinate = {
  key: string
  column: string
  columnIndex: number
  row: number
}

export type PositionMatrix = {
  columns: string[]
  rows: number[]
  positions: string[]
  columnCount: number
  rowCount: number
  cellCount: number
}

const LEGACY_COLUMNS: Record<string, number> = { C: 1, M: 2, E: 3 }
const LEGACY_ROWS: Record<string, number> = { U: 1, M: 2, B: 3 }
const LEGACY_COMPACT_COLUMNS: Record<string, number> = {
  I: 1,
  A: 1,
  M: 2,
  B: 2,
  E: 3,
  C: 3,
}
const LEGACY_COMPACT_ROWS: Record<string, number> = { U: 1, M: 2, B: 3, D: 3 }

export function excelColumnToNumber(value: string): number {
  let result = 0
  for (const char of value.trim().toUpperCase()) {
    if (char < 'A' || char > 'Z') return 0
    result = result * 26 + char.charCodeAt(0) - 64
  }
  return result
}

export function numberToExcelColumn(value: number): string {
  let current = Math.floor(value)
  if (!Number.isFinite(current) || current < 1) return ''
  let result = ''
  while (current > 0) {
    current -= 1
    result = String.fromCharCode(65 + (current % 26)) + result
    current = Math.floor(current / 26)
  }
  return result
}

export function parsePosition(value: string): PositionCoordinate | null {
  const match = value.trim().toUpperCase().match(/^([A-Z]+)-([1-9]\d*)$/)
  if (!match) return null
  const columnIndex = excelColumnToNumber(match[1])
  const row = Number.parseInt(match[2], 10)
  if (!columnIndex || !Number.isSafeInteger(row) || row < 1) return null
  const column = numberToExcelColumn(columnIndex)
  return {
    key: `${column}-${row}`,
    column,
    columnIndex,
    row,
  }
}

export function normalizePosition(value: string): string {
  const trimmed = value.trim().toUpperCase()
  if (!trimmed) return ''

  const canonical = parsePosition(trimmed)
  if (canonical) return canonical.key

  const numeric = trimmed.match(/^([1-9])$/)
  if (numeric) {
    const index = Number.parseInt(numeric[1], 10) - 1
    return `${numberToExcelColumn((index % 3) + 1)}-${Math.floor(index / 3) + 1}`
  }

  const legacy = trimmed.match(/^([CME])\s*[-_]\s*([UMB])$/)
  if (legacy) {
    return `${numberToExcelColumn(LEGACY_COLUMNS[legacy[1]])}-${LEGACY_ROWS[legacy[2]]}`
  }

  // Historical imports only inspected the first two compact characters, so
  // keep accepting suffixes that older customer folders may contain.
  const compact = trimmed.replace(/[\s_-]+/g, '').match(/^([EIMABC])([UMBD])/)
  if (compact) {
    return `${numberToExcelColumn(LEGACY_COMPACT_COLUMNS[compact[1]])}-${LEGACY_COMPACT_ROWS[compact[2]]}`
  }

  return ''
}

export function comparePositions(a: string, b: string): number {
  const left = parsePosition(a)
  const right = parsePosition(b)
  if (!left && !right) return a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' })
  if (!left) return 1
  if (!right) return -1
  return left.row - right.row || left.columnIndex - right.columnIndex
}

export function positionKey(column: string, row: number): string {
  return `${column}-${row}`
}

export function buildPositionMatrix(values: Iterable<string>): PositionMatrix {
  const parsed = Array.from(values)
    .map(parsePosition)
    .filter((value): value is PositionCoordinate => value !== null)

  if (!parsed.length) {
    return {
      columns: [],
      rows: [],
      positions: [],
      columnCount: 0,
      rowCount: 0,
      cellCount: 0,
    }
  }

  const maxColumn = Math.max(...parsed.map((item) => item.columnIndex))
  const maxRow = Math.max(...parsed.map((item) => item.row))
  const columns = Array.from(
    { length: maxColumn },
    (_, index) => numberToExcelColumn(index + 1),
  )
  const rows = Array.from({ length: maxRow }, (_, index) => index + 1)
  const positions = rows.flatMap((row) => columns.map((column) => positionKey(column, row)))

  return {
    columns,
    rows,
    positions,
    columnCount: columns.length,
    rowCount: rows.length,
    cellCount: columns.length * rows.length,
  }
}
