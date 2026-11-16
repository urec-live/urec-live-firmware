import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["IN"],
  pin2: ["GND"],
  pin3: ["EN"],
  pin4: ["NC"],
  pin5: ["OUT"]
} as const

const pinAttributes = {
  pin2: {requiresGround: true},
  pin4: {doNotConnect: true}
} as const

export const TPS7A0233PDBVR = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      pinAttributes={pinAttributes}
      supplierPartNumbers={{
  "jlcpcb": [
    "C2887324"
  ]
}}
      manufacturerPartNumber="TPS7A0233PDBVR"
      // Hand-corrected after import: TI's DBV0005A land pattern (SBVS277C, p. 46): pitch 0.95,
      // pad 0.6 x 1.1, pad-row spacing 2.6. The imported EasyEDA pattern had the rows 2.2 mm apart.
      footprint="sot25_w2.6mm_pl1.1mm_pin1location(rightside,bottom)"
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2887324.obj?uuid=8c971aea3af54c53b74baeb1f489d393",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2887324.step?uuid=8c971aea3af54c53b74baeb1f489d393",
        pcbRotationOffset: 90,
        modelOriginPosition: { x: -0.000012699999956566899, y: 0.00006349999989652133, z: -0.7 },
      }}
      {...props}
    />
  )
}