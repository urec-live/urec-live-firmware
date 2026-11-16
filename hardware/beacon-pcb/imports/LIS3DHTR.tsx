import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["VDD_IO"],
  pin2: ["NC1"],
  pin3: ["NC2"],
  pin4: ["SCL","SPC"],
  pin5: ["GND1"],
  pin6: ["SDA","SDI","SDO1"],
  pin7: ["SDO2","SA0"],
  pin8: ["CS"],
  pin9: ["INT2"],
  pin10: ["RES"],
  pin11: ["INT1"],
  pin12: ["GND2"],
  pin13: ["ADC3"],
  pin14: ["VDD"],
  pin15: ["ADC2"],
  pin16: ["ADC1"]
} as const

const pinAttributes = {
  pin2: {doNotConnect: true},
  pin3: {doNotConnect: true},
  pin5: {requiresGround: true, mustBeConnected: true},
  pin12: {requiresGround: true, mustBeConnected: true},
  pin14: {requiresPower: true, mustBeConnected: true, shouldHaveDecouplingCapacitor: true},
  pin1: {requiresPower: true, mustBeConnected: true, shouldHaveDecouplingCapacitor: true},
  pin4: {isInput: true, capabilities: ["i2c_scl","spi_sck"]},
  pin6: {isInput: true, isOutput: true, isBidirectional: true, capabilities: ["i2c_sda","spi_mosi"]},
  pin7: {isInput: true, isOutput: true, isBidirectional: true, capabilities: ["spi_miso"], canUseInternalPullup: true},
  pin8: {isInput: true, capabilities: ["spi_cs"]},
  pin9: {isOutput: true, canUsePushPull: true},
  pin10: {requiresGround: true, mustBeConnected: true},
  pin11: {isOutput: true, canUsePushPull: true},
  pin13: {isInput: true},
  pin15: {isInput: true},
  pin16: {isInput: true}
} satisfies NonNullable<ChipProps["pinAttributes"]>

export const LIS3DHTR = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      pinAttributes={pinAttributes}
      supplierPartNumbers={{
  "jlcpcb": [
    "C15134"
  ]
}}
      manufacturerPartNumber="LIS3DHTR"
      footprint="lga16_grid5x3_pillpads_w3.6mm_pl0.8mm"
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C15134.obj?uuid=c4b5277f94624e3ba038828676140c3b",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C15134.step?uuid=c4b5277f94624e3ba038828676140c3b",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0, y: 0, z: 0 },
      }}
      {...props}
    />
  )
}