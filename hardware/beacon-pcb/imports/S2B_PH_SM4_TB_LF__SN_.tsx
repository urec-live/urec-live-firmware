import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["pin1"],
  pin2: ["pin2"],
  pin3: ["pin3"],
  pin4: ["pin4"]
} as const

const pinAttributes = {
  pin1: {isPassive: true},
  pin2: {isPassive: true}
} satisfies NonNullable<ChipProps["pinAttributes"]>

export const S2B_PH_SM4_TB_LF__SN_ = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      pinAttributes={pinAttributes}
      supplierPartNumbers={{
  "jlcpcb": [
    "C295747"
  ]
}}
      manufacturerPartNumber="S2B-PH-SM4-TB(LF)(SN)"
      footprint="fpc2_p2mm_pw1mm_pl3.8mm_mpx6.7mm_mpy5.85mm_mpw1.5mm_mpl3.4mm"
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C295747.obj?uuid=e009435048914ef2b30a218c3065ed28",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C295747.step?uuid=e009435048914ef2b30a218c3065ed28",
        pcbRotationOffset: 0,
        // Hand-corrected after import. The footprinter string above puts the footprint origin on
        // the signal pads, 2.825 mm behind the centre of the pad pattern that the EasyEDA model
        // was positioned against, so the imported y (2.85) drew the housing 2.825 mm too far
        // back. Checked in the exported GLB: housing rear just behind the signal pads, solder
        // tabs inside the housing sides, mouth 0.6 mm inside the board edge.
        modelOriginPosition: { x: -0.9999872999999297, y: 2.8499996000001833 + 2.825, z: 0 },
      }}
      {...props}
    />
  )
}