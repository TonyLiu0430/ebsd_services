/**
 * Round a non-negative chart maximum up to the nearest 10 while preserving
 * exact multiples of 10. The floor is used when the data is empty or smaller.
 */
export function roundUpToNearestTen(value, floor = 10) {
  const numericValue = Number(value)
  const numericFloor = Number(floor)
  const safeValue = Number.isFinite(numericValue) ? Math.max(0, numericValue) : 0
  const safeFloor = Number.isFinite(numericFloor) ? Math.max(0, numericFloor) : 0
  return Math.max(safeFloor, Math.ceil(safeValue / 10) * 10)
}
