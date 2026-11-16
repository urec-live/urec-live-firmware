import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["STAT"],
  pin2: ["VSS"],
  pin3: ["VBAT"],
  pin4: ["VDD"],
  pin5: ["PROG"]
} as const

const pinAttributes = {
  pin2: {isPassive: true, requiresGround: true, mustBeConnected: true},
  pin4: {isInput: true, requiresPower: true, mustBeConnected: true, shouldHaveDecouplingCapacitor: true, recommendedDecouplingCapacitorCapacitance: "4.7uF"},
  pin1: {isOutput: true, canUseTriState: true},
  pin3: {isOutput: true, providesPower: true, shouldHaveDecouplingCapacitor: true, recommendedDecouplingCapacitorCapacitance: "4.7uF"},
  pin5: {isInput: true}
} satisfies NonNullable<ChipProps["pinAttributes"]>

export const MCP73831T_2ACI_OT = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      pinAttributes={pinAttributes}
      supplierPartNumbers={{
  "jlcpcb": [
    "C424093"
  ]
}}
      manufacturerPartNumber="MCP73831T-2ACI/OT"
      // Hand-corrected after import: Microchip's recommended SOT-23-5 land pattern (DS20001984H,
      // p. 24): pitch 0.95, pad 0.60 x 1.10, pad-row spacing 2.80 (overall 3.90). The imported
      // EasyEDA pattern had the rows 2.31 mm apart.
      footprint="dfn6_missing(5)_p0.95mm_w3.9mm_pw0.6mm_pl1.1mm_pin1location(leftside,bottom)"
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C424093.obj?uuid=460193f9bf2d42e58cf3c2f675b07dc6",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C424093.step?uuid=460193f9bf2d42e58cf3c2f675b07dc6",
        pcbRotationOffset: 90,
        modelOriginPosition: { x: 0.004489450000050965, y: 0.000012700000070253736, z: -0.049083 },
      }}
      {...props}
    />
  )
}