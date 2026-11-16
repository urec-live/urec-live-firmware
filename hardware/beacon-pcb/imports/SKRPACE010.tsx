import type { PushButtonProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["pin1"],
  pin2: ["pin2"],
  pin3: ["pin3"],
  pin4: ["pin4"]
} as const

const pinAttributes = {
  pin1: {isPassive: true},
  pin2: {isPassive: true},
  pin3: {isPassive: true},
  pin4: {isPassive: true}
} satisfies NonNullable<PushButtonProps["pinAttributes"]>

export const SKRPACE010 = (props: PushButtonProps<typeof pinLabels>) => {
  const { name = "SW1", ...restProps } = props

  return (
    <pushbutton
      name={name}
      pinLabels={pinLabels}
      supplierPartNumbers={{
  "jlcpcb": [
    "C139797"
  ]
}}
      manufacturerPartNumber="SKRPACE010"
      pinAttributes={pinAttributes}
      internallyConnectedPins={[["1","2"],["3","4"]]}
      footprint="smdpushbutton"
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C139797.obj?uuid=00d848a7e8384bbd9286566957e8bb9c",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C139797.step?uuid=00d848a7e8384bbd9286566957e8bb9c",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0.000012700000070253736, y: 0.00012700000002041634, z: -0.01 },
      }}
      {...restProps}
    />
  )
}