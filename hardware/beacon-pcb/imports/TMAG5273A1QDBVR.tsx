import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["SCL"],
  pin2: ["GND"],
  pin3: ["GND_TEST"],
  pin4: ["VCC"],
  pin5: ["N_INT"],
  pin6: ["SDA"]
} as const

const pinAttributes = {
  pin2: {requiresGround: true},
  pin4: {requiresPower: true}
} as const

export const TMAG5273A1QDBVR = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      pinAttributes={pinAttributes}
      supplierPartNumbers={{
  "jlcpcb": [
    "C3716049"
  ]
}}
      manufacturerPartNumber="TMAG5273A1QDBVR"
      // Hand-corrected after import: TI's DBV (SOT-23-6) land pattern: pitch 0.95, pad 0.6 x 1.1,
      // pad-row spacing 2.6 (overall 3.7). The imported EasyEDA pattern was 0.1 mm wider overall.
      footprint="soic6_p0.95mm_w3.7mm_pl1.1mm_pin1location(rightside,bottom)"
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C3716049.obj?uuid=229b69761e2c45dba6a83d8866dec72d",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C3716049.step?uuid=229b69761e2c45dba6a83d8866dec72d",
        pcbRotationOffset: 180,
        modelOriginPosition: { x: 0.000025399999913133797, y: -0.0000889000000370288, z: -0.048939 },
      }}
      {...props}
    />
  )
}