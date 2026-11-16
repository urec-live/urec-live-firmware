import type { CrystalProps } from "@tscircuit/props"

type ImportedCrystalProps = Omit<CrystalProps, "frequency" | "pinVariant">

export const SF32WK32768D91T004 = (props: ImportedCrystalProps) => {
  const { name = "X1", ...restProps } = props

  return (
    <crystal
      name={name}
      frequency="32.768kHz"
      pinVariant="two_pin"
      supplierPartNumbers={{
  "jlcpcb": [
    "C5374762"
  ]
}}
      manufacturerPartNumber="SF32WK32768D91T004"
      footprint="smdpads2_p2.5403mm_pw1.05mm_ph1.7mm_cyw4.0903mm_cyh2.2mm"
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C5374762.obj?uuid=eac14d4facdb45dfa3b66d00e2a3c6e4",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C5374762.step?uuid=eac14d4facdb45dfa3b66d00e2a3c6e4",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0.00007619999996677507, y: 0.0000889000000370288, z: -0.01 },
      }}
      {...restProps}
    />
  )
}