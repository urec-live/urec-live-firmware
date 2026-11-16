// Static assets imported by the circuit resolve to a URL string at build time.
declare module "*.glb" {
  const url: string
  export default url
}
