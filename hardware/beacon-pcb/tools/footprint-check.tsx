// Footprint inspection board: one of each imported part, spaced apart, unrouted.
// Used with tools/dump-pads.ts to check pad geometry against datasheets.
import { MDBT50Q_1MV2 } from "../imports/MDBT50Q_1MV2"
import { LIS3DHTR } from "../imports/LIS3DHTR"
import { LIS2MDLTR } from "../imports/LIS2MDLTR"
import { TMAG5273A1QDBVR } from "../imports/TMAG5273A1QDBVR"
import { GD25Q16CEIGR } from "../imports/GD25Q16CEIGR"
import { MCP73831T_2ACI_OT } from "../imports/MCP73831T_2ACI_OT"
import { TPS7A0233PDBVR } from "../imports/TPS7A0233PDBVR"
import { USBLC6_2SC6 } from "../imports/USBLC6_2SC6"
import { S2B_PH_SM4_TB_LF__SN_ } from "../imports/S2B_PH_SM4_TB_LF__SN_"
import { SF32WK32768D91T004 } from "../imports/SF32WK32768D91T004"
import { SKRPACE010 } from "../imports/SKRPACE010"
import { KT_0603R } from "../imports/KT_0603R"
import { A_19_217_BHC_ZL1M2RY_3T } from "../imports/A_19_217_BHC_ZL1M2RY_3T"

export default () => (
  <board width="120mm" height="60mm" routingDisabled>
    <MDBT50Q_1MV2 name="U1" pcbX={-50} pcbY={0} />
    <LIS3DHTR name="U2" pcbX={-35} pcbY={0} />
    <LIS2MDLTR name="U3" pcbX={-25} pcbY={0} />
    <TMAG5273A1QDBVR name="U4" pcbX={-15} pcbY={0} />
    <GD25Q16CEIGR name="U5" pcbX={-5} pcbY={0} />
    <MCP73831T_2ACI_OT name="U6" pcbX={5} pcbY={0} />
    <TPS7A0233PDBVR name="U7" pcbX={15} pcbY={0} />
    <USBLC6_2SC6 name="U8" pcbX={25} pcbY={0} />
    <S2B_PH_SM4_TB_LF__SN_ name="J2" pcbX={40} pcbY={0} />
    <SF32WK32768D91T004 name="Y1" loadCapacitance="9pF" pcbX={-35} pcbY={-15} />
    <SKRPACE010 name="SW1" pcbX={-25} pcbY={-15} />
    <KT_0603R name="D1" pcbX={-15} pcbY={-15} />
    <A_19_217_BHC_ZL1M2RY_3T name="D2" pcbX={-5} pcbY={-15} />
    <connector name="J1" standard="usb_c" pcbX={10} pcbY={-15} />
    <chip
      name="J3"
      footprint="kicad:Connector/Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical"
      pcbX={30}
      pcbY={-15}
    />
    <resistor name="R1" resistance="10k" footprint="0402" pcbX={-35} pcbY={15} />
    <capacitor name="C1" capacitance="10uF" footprint="0603" pcbX={-25} pcbY={15} />
    <testpoint name="TP1" footprintVariant="pad" padShape="circle" padDiameter="1.2mm" pcbX={-15} pcbY={15} />
    <hole name="H1" diameter="2.2mm" pcbX={-5} pcbY={15} />
  </board>
)


