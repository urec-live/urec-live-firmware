import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["N_CS"],
  pin2: ["SO","IO1"],
  pin3: ["N_WP","IO2"],
  pin4: ["VSS"],
  pin5: ["SI","IO0"],
  pin6: ["SCLK"],
  pin7: ["N_HOLD","IO3"],
  pin8: ["VCC"],
  pin9: ["EP"]
} as const

const pinAttributes = {
  pin4: {requiresGround: true},
  pin8: {requiresPower: true}
} as const

const footprinterPinLabels = {
  ...pinLabels,
  "pin9": [...pinLabels["pin9"], "thermalpad"],
} as const

export const GD25Q16CEIGR = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={footprinterPinLabels}
      pinAttributes={pinAttributes}
      supplierPartNumbers={{
  "jlcpcb": [
    "C2973793"
  ]
}}
      manufacturerPartNumber="GD25Q16CEIGR"
      footprint="dfn8_thermalpad0.3mmx1.7mm_p0.5mm_w3.3mm_pw0.28mm_pl0.6mm_pin1location(leftside,bottom)"
      
      {...props}
    />
  )
}