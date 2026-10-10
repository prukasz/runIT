declare module 'imagetracerjs' {
  interface TraceOptions {
    numberofcolors?: number
    colorquantcycles?: number
    ltres?: number
    qtres?: number
    pathomit?: number
    blurradius?: number
    blurdelta?: number
    roundcoords?: number
    viewbox?: boolean
    scale?: number
    strokewidth?: number
  }
  const ImageTracer: { imagedataToSVG(data: ImageData, options?: TraceOptions): string }
  export default ImageTracer
}
