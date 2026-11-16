// Tag-Connect TC2030-NL ("no legs") pogo-pin programming footprint: six bare pads and three
// unplated alignment holes. There is no part to buy or place; the cable presses onto the pads.
//
// Geometry source: KiCad library footprint
//   Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical
// (pads 0.787 mm dia on a 1.27 mm grid, alignment holes 0.991 mm dia), which follows
// Tag-Connect's TC2030-NL PCB footprint drawing. It is redrawn here only to drop the KiCad
// fabrication-layer text. Coordinates are in the footprint's own frame, Y up.
//
// Pin use follows Tag-Connect's TC2030-CTX (ARM Cortex SWD) cable:
//   1 VTref   2 SWDIO   3 nRESET   4 SWCLK   5 GND   6 SWO
import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["VTREF"],
  pin2: ["SWDIO"],
  pin3: ["NRESET"],
  pin4: ["SWDCLK"],
  pin5: ["GND"],
  pin6: ["SWO"],
} as const

const PAD = "0.787mm"
const ALIGN_HOLE = "0.9906mm"

export const TagConnectTC2030NL = (props: ChipProps<typeof pinLabels>) => (
  <chip
    pinLabels={pinLabels}
    manufacturerPartNumber="TC2030-NL footprint (no part)"
    cadModel={null}
    doNotPlace
    footprint={
      <footprint>
        <smtpad portHints={["pin1"]} shape="circle" radius="0.3935mm" pcbX="-1.27mm" pcbY="-0.635mm" />
        <smtpad portHints={["pin2"]} shape="circle" radius="0.3935mm" pcbX="-1.27mm" pcbY="0.635mm" />
        <smtpad portHints={["pin3"]} shape="circle" radius="0.3935mm" pcbX="0mm" pcbY="-0.635mm" />
        <smtpad portHints={["pin4"]} shape="circle" radius="0.3935mm" pcbX="0mm" pcbY="0.635mm" />
        <smtpad portHints={["pin5"]} shape="circle" radius="0.3935mm" pcbX="1.27mm" pcbY="-0.635mm" />
        <smtpad portHints={["pin6"]} shape="circle" radius="0.3935mm" pcbX="1.27mm" pcbY="0.635mm" />
        <hole diameter={ALIGN_HOLE} pcbX="-2.54mm" pcbY="0mm" />
        <hole diameter={ALIGN_HOLE} pcbX="2.54mm" pcbY="1.016mm" />
        <hole diameter={ALIGN_HOLE} pcbX="2.54mm" pcbY="-1.016mm" />
        <courtyardrect pcbX={0} pcbY={0} width={7} height={4} strokeWidth={0.05} />
      </footprint>
    }
    {...props}
  />
)

// Kept for reference so the pad diameter above is traceable to one number.
export const TC2030_PAD_DIAMETER = PAD
