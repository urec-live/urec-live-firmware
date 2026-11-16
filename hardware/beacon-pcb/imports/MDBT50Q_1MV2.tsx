import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["GND1"],
  pin2: ["GND2"],
  pin3: ["P1_10"],
  pin4: ["P1_11"],
  pin5: ["P1_12"],
  pin6: ["P1_13"],
  pin7: ["P1_14"],
  pin8: ["P1_15"],
  pin9: ["P0_03","AIN1"],
  pin10: ["P0_29","AIN5"],
  pin11: ["P0_02","AIN0"],
  pin12: ["P0_31","AIN7"],
  pin13: ["P0_28","AIN4"],
  pin14: ["P0_30","AIN6"],
  pin15: ["GND3"],
  pin16: ["P0_27"],
  pin17: ["P0_00","XL1"],
  pin18: ["P0_01","XL2"],
  pin19: ["P0_26"],
  pin20: ["P0_04","AIN2"],
  pin21: ["P0_05","AIN3"],
  pin22: ["P0_06"],
  pin23: ["P0_07","TRACECLK"],
  pin24: ["P0_08"],
  pin25: ["P1_08"],
  pin26: ["P1_09","TRACEDATA3"],
  pin27: ["P0_11","TRACEDATA2"],
  pin28: ["VDD"],
  pin29: ["P0_12","TRACEDATA1"],
  pin30: ["VDDH"],
  pin31: ["DCCH"],
  pin32: ["VBUS"],
  pin33: ["GND4"],
  pin34: ["D_NEG"],
  pin35: ["D_POS"],
  pin36: ["P0_14"],
  pin37: ["P0_13"],
  pin38: ["P0_16"],
  pin39: ["P0_15"],
  pin40: ["P0_18","nRESET"],
  pin41: ["P0_17"],
  pin42: ["P0_19"],
  pin43: ["P0_21"],
  pin44: ["P0_20"],
  pin45: ["P0_23"],
  pin46: ["P0_22"],
  pin47: ["P1_00","TRACEDATA0"],
  pin48: ["P0_24"],
  pin49: ["P0_25"],
  pin50: ["P1_02"],
  pin51: ["SWDIO"],
  pin52: ["P0_09","NFC1"],
  pin53: ["SWDCLK"],
  pin54: ["P0_10","NFC2"],
  pin55: ["GND5"],
  pin56: ["P1_04"],
  pin57: ["P1_06"],
  pin58: ["P1_07"],
  pin59: ["P1_05"],
  pin60: ["P1_03"],
  pin61: ["P1_01"]
} as const

const pinAttributes = {
  pin1: {requiresGround: true},
  pin2: {requiresGround: true},
  pin15: {requiresGround: true},
  pin28: {requiresPower: true},
  pin32: {requiresPower: true},
  pin33: {requiresGround: true},
  pin55: {requiresGround: true}
} as const

export const MDBT50Q_1MV2 = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      pinAttributes={pinAttributes}
      supplierPartNumbers={{
  "jlcpcb": [
    "C5118826"
  ]
}}
      manufacturerPartNumber="MDBT50Q-1MV2"
      footprint={<footprint>
        <smtpad portHints={["pin1"]} pcbX="-4.650105mm" pcbY="5.5000779mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin2"]} pcbX="-4.650105mm" pcbY="4.4000039mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin3"]} pcbX="-4.650105mm" pcbY="3.5999039mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin4"]} pcbX="-4.650105mm" pcbY="1.9999579mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin5"]} pcbX="-3.750183mm" pcbY="1.5999079mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin6"]} pcbX="-4.650105mm" pcbY="1.2001119mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin7"]} pcbX="-3.750183mm" pcbY="0.8000619mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin8"]} pcbX="-4.650105mm" pcbY="0.4000119mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin9"]} pcbX="-3.750183mm" pcbY="-0.0000381mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin10"]} pcbX="-4.650105mm" pcbY="-0.4000881mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin11"]} pcbX="-3.750183mm" pcbY="-0.8001381mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin12"]} pcbX="-4.650105mm" pcbY="-1.1999341mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin13"]} pcbX="-3.750183mm" pcbY="-1.5999841mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin14"]} pcbX="-4.650105mm" pcbY="-2.0000341mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin15"]} pcbX="-4.799965mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin16"]} pcbX="-4.000119mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin17"]} pcbX="-3.200019mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin18"]} pcbX="-2.400173mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin19"]} pcbX="-2.000123mm" pcbY="-4.4999021mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin20"]} pcbX="-1.600073mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin21"]} pcbX="-1.200023mm" pcbY="-4.4999021mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin22"]} pcbX="-0.799973mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin23"]} pcbX="-0.400177mm" pcbY="-4.4999021mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin24"]} pcbX="-0.000127mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin25"]} pcbX="0.399923mm" pcbY="-4.4999021mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin26"]} pcbX="0.799973mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin27"]} pcbX="1.200023mm" pcbY="-4.4999021mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin28"]} pcbX="1.600073mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin29"]} pcbX="1.999869mm" pcbY="-4.4999021mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin30"]} pcbX="2.399919mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin31"]} pcbX="3.200019mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin32"]} pcbX="3.999865mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin33"]} pcbX="4.799965mm" pcbY="-5.4000781mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin34"]} pcbX="4.649851mm" pcbY="-4.4000801mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin35"]} pcbX="4.649851mm" pcbY="-3.5999801mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin36"]} pcbX="3.749929mm" pcbY="-3.1999301mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin37"]} pcbX="4.649851mm" pcbY="-2.8001341mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin38"]} pcbX="3.749929mm" pcbY="-2.4000841mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin39"]} pcbX="4.649851mm" pcbY="-2.0000341mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin40"]} pcbX="3.749929mm" pcbY="-1.5999841mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin41"]} pcbX="4.649851mm" pcbY="-1.1999341mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin42"]} pcbX="3.749929mm" pcbY="-0.7998841mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin43"]} pcbX="3.749929mm" pcbY="-0.0000381mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin44"]} pcbX="4.649851mm" pcbY="0.4000119mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin45"]} pcbX="3.749929mm" pcbY="0.8000619mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin46"]} pcbX="4.649851mm" pcbY="1.2001119mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin47"]} pcbX="3.749929mm" pcbY="1.6001619mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin48"]} pcbX="4.649851mm" pcbY="1.9999579mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin49"]} pcbX="3.749929mm" pcbY="2.4000079mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin50"]} pcbX="3.749929mm" pcbY="3.2001079mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin51"]} pcbX="4.649851mm" pcbY="3.5999039mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin52"]} pcbX="3.749929mm" pcbY="3.9999539mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin53"]} pcbX="4.649851mm" pcbY="4.4000039mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin54"]} pcbX="3.749929mm" pcbY="4.8000539mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin55"]} pcbX="4.649851mm" pcbY="5.5000779mm" width="0.5999988mm" height="0.3999992mm" shape="rect" />
<smtpad portHints={["pin56"]} pcbX="-2.000123mm" pcbY="1.2001119mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin57"]} pcbX="-1.200023mm" pcbY="1.1998579mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin58"]} pcbX="-0.400177mm" pcbY="1.2001119mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin59"]} pcbX="0.399923mm" pcbY="1.1998579mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin60"]} pcbX="1.200023mm" pcbY="1.1998579mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin61"]} pcbX="1.999869mm" pcbY="1.2001119mm" width="0.3999992mm" height="0.5999988mm" shape="rect" />
<silkscreenpath route={[{"x":-5.250027599999953,"y":5.829998499999988},{"x":5.2499259999999595,"y":5.829998499999988}]} />
<silkscreenpath route={[{"x":5.25010379999992,"y":9.499892100000011},{"x":-5.249849800000106,"y":9.499892100000011},{"x":-5.249849800000106,"y":-6.000127700000007},{"x":5.25010379999992,"y":-6.000127700000007},{"x":5.25010379999992,"y":9.499892100000011}]} />
<silkscreencircle pcbX="-5.620131mm" pcbY="5.5899939mm" radius="0.127mm" />
<silkscreentext text="{NAME}" pcbX="-0.248285mm" pcbY="10.5357823mm" anchorAlignment="center" fontSize="1mm" />
<courtyardoutline outline={[{"x":-5.997385000000008,"y":9.785782299999937},{"x":5.500815000000102,"y":9.785782299999937},{"x":5.500815000000102,"y":-6.233617700000082},{"x":-5.997385000000008,"y":-6.233617700000082},{"x":-5.997385000000008,"y":9.785782299999937}]} />
      </footprint>}
      
      {...props}
    />
  )
}