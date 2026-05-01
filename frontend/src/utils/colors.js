const DEVICE_COLOURS = [
  "#4f8ef7", "#22d3ee", "#a855f7", "#22c55e",
  "#f97316", "#eab308", "#ec4899", "#06b6d4",
];
let colourIndex = 0;
const colourMap = {};

export function deviceColour(id) {
  if (!colourMap[id])
    colourMap[id] = DEVICE_COLOURS[colourIndex++ % DEVICE_COLOURS.length];
  return colourMap[id];
}
