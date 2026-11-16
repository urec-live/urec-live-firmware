import type { LedProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["cathode","neg"],
  pin2: ["anode","pos"]
} as const

export const A_19_217_BHC_ZL1M2RY_3T = (props: LedProps) => {
  const { name = "LED1", ...restProps } = props

  return (
    <led
      name={name}
      pinLabels={pinLabels}
      supplierPartNumbers={{
  "jlcpcb": [
    "C72041"
  ]
}}
      manufacturerPartNumber="19-217/BHC-ZL1M2RY/3T"
      footprint="diode_p1.4399mm_pw0.8mm_ph0.8999mm_rounded0_cyw2.7399mm_cyh1.3999mm_cathodepin1"
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C72041.obj?uuid=72af388e57ea4986aa549e090154fbdf",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C72041.step?uuid=72af388e57ea4986aa549e090154fbdf",
        pcbRotationOffset: 180,
        modelOriginPosition: { x: -0.12141200000007757, y: 0, z: -0.04 },
      }}
      {...restProps}
    />
  )
}