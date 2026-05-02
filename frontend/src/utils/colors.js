const DEVICE_COLOURS = [
  "#7c3aed", "#ec4899", "#8b5cf6", "#a78bfa",
  "#f97316", "#eab308", "#22d3ee", "#06b6d4",
];
let colourIndex = 0;
const colourMap = {};

export function deviceColour(id) {
  if (!colourMap[id])
    colourMap[id] = DEVICE_COLOURS[colourIndex++ % DEVICE_COLOURS.length];
  return colourMap[id];
}
