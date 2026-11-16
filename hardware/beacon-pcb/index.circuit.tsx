// tsci entry point: the GetMeFit beacon concept board (EVT-0). NOT FOR FABRICATION.
// The board itself is in Beacon.tsx; geometry constants are in layout.ts.
//
// Routing is on, with plane pours, so the model looks like a board. The routes come straight
// from tscircuit's autorouter and are for illustration only: nobody has reviewed them.
// DESIGN-NOTES.md ("Routing") says what that means. For placement and ratsnest lines only,
// render <Beacon /> with its defaults (routing off, no pours).
import { Beacon } from "./Beacon"

export default () => <Beacon routingDisabled={false} pours />
