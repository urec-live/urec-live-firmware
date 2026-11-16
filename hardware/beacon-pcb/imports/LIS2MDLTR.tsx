import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["SCL","SPC"],
  pin2: ["NC3"],
  pin3: ["CS"],
  pin4: ["SDA","SDI","SDO2"],
  pin5: ["C1"],
  pin6: ["GND2"],
  pin7: ["INT","DRDY","SDO1"],
  pin8: ["GND1"],
  pin9: ["Vdd"],
  pin10: ["Vdd_IO"],
  pin11: ["NC2"],
  pin12: ["NC1"]
} as const

const pinAttributes = {
  pin2: {doNotConnect: true},
  pin6: {requiresGround: true},
  pin8: {requiresGround: true},
  pin9: {requiresPower: true},
  pin11: {doNotConnect: true},
  pin12: {doNotConnect: true}
} as const

export const LIS2MDLTR = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      pinAttributes={pinAttributes}
      supplierPartNumbers={{
  "jlcpcb": [
    "C919695"
  ]
}}
      manufacturerPartNumber="LIS2MDLTR"
      footprint={<footprint>
        <smtpad portHints={["pin12"]} pcbX="-0.750062mm" pcbY="-0.249936mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin11"]} pcbX="-0.750062mm" pcbY="0.249936mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin10"]} pcbX="-0.750062mm" pcbY="0.750062mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin9"]} pcbX="-0.249936mm" pcbY="0.750062mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin8"]} pcbX="0.249936mm" pcbY="0.750062mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin7"]} pcbX="0.750062mm" pcbY="0.750062mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin6"]} pcbX="0.750062mm" pcbY="0.249936mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin5"]} pcbX="0.750062mm" pcbY="-0.249936mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin4"]} pcbX="0.750062mm" pcbY="-0.750062mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin3"]} pcbX="0.249936mm" pcbY="-0.750062mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin2"]} pcbX="-0.249936mm" pcbY="-0.750062mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<smtpad portHints={["pin1"]} pcbX="-0.750062mm" pcbY="-0.750062mm" width="0.2999994mm" height="0.2999994mm" shape="rect" />
<silkscreenpath route={[{"x":-1.1211306000001287,"y":-1.0791190000001052},{"x":-0.9993376000001035,"y":-1.0791190000001052}]} />
<silkscreenpath route={[{"x":0.7111237999998821,"y":-1.0919460000000072},{"x":1.0921237999998539,"y":-1.0919460000000072},{"x":1.0921237999998539,"y":-0.7109460000000354}]} />
<silkscreenpath route={[{"x":0.7111237999998821,"y":1.0924031999999215},{"x":1.0921237999998539,"y":1.0924031999999215},{"x":1.0921237999998539,"y":0.7114031999999497}]} />
<silkscreenpath route={[{"x":-0.7112762000001567,"y":1.0924031999999215},{"x":-1.0922762000001285,"y":1.0924031999999215},{"x":-1.0922762000001285,"y":0.7114031999999497}]} />
<silkscreenpath route={[{"x":-1.1176508000000922,"y":-1.041171400000053},{"x":-1.1176508000000922,"y":-0.6093714000001},{"x":-1.1176508000000922,"y":-1.0791190000001052}]} />
<silkscreencircle pcbX="-1.2446mm" pcbY="-1.320546mm" radius="0.056896mm" />
<silkscreentext text="{NAME}" pcbX="-0.114554mm" pcbY="2.099058mm" anchorAlignment="center" fontSize="1mm" />
<courtyardoutline outline={[{"x":-1.2500742000000855,"y":1.2500741999999718},{"x":1.249921799999811,"y":1.2500741999999718},{"x":1.249921799999811,"y":-1.2499218000000383},{"x":-1.2500742000000855,"y":-1.2499218000000383},{"x":-1.2500742000000855,"y":1.2500741999999718}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C919695.obj?uuid=ea8d6df67cc840cc833e11ac6f10a99b",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C919695.step?uuid=ea8d6df67cc840cc833e11ac6f10a99b",
        pcbRotationOffset: 90,
        modelOriginPosition: { x: -0.00007619999996677507, y: -0.0000762000000804619, z: -0.24 },
      }}
      {...props}
    />
  )
}