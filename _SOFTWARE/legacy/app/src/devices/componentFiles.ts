import type { DevicePort } from '../domain/descriptors'
import { layoutFileName, resolveLayout, serializeLayout, variantDir, variantOfFile } from '../domain/devices/componentLayout'
import type { ComponentLayout, LayoutResult } from '../domain/devices/componentLayout'

/*
 * Client of the dev server's component-file endpoint (app/vite/componentFiles.ts): lists, reads and saves a device's
 * layout files and pictures in components/devices/<device>/canvas/<variant>/ (the pins csv stays in the device root). Only the dev server has the endpoint, so every call
 * can fail and the caller shows the error.
 */

const ROUTE = '/__runit/component-files'
const url = (deviceId: string, file = ''): string => `${ROUTE}/${encodeURIComponent(deviceId)}${file ? `/${file}` : ''}`

async function check(response: Response): Promise<Response> {
  if (!response.ok) throw new Error(`Component files: ${response.status} ${(await response.text()).trim() || response.statusText}`)
  return response
}

export interface ComponentFiles {
  /** Layout variant IDs found in the folder. */
  readonly variants: readonly string[]
}

export async function listComponentFiles(deviceId: string): Promise<ComponentFiles> {
  const files = (await (await check(await fetch(url(deviceId)))).json()) as string[]
  return { variants: files.flatMap((file) => variantOfFile(file) ?? []) }
}

/** One variant's layout checked against the device's pins. */
export async function loadLayout(deviceId: string, variant: string, ports: readonly DevicePort[]): Promise<LayoutResult> {
  const response = await fetch(url(deviceId, layoutFileName(variant)))
  if (response.status === 404) return { problems: [`No layout saved for ${variant}.`] }
  await check(response)
  try {
    return resolveLayout(await response.json(), ports)
  } catch {
    return { problems: [`${layoutFileName(variant)} is not valid JSON.`] }
  }
}

const toBase64 = async (blob: Blob): Promise<string> => {
  const bytes = new Uint8Array(await blob.arrayBuffer())
  let text = ''
  for (let i = 0; i < bytes.length; i += 0x8000) text += String.fromCharCode(...bytes.subarray(i, i + 0x8000))
  return btoa(text)
}

/**
 * Saves a variant into `canvas/<variant>/`: its `layout.json` and the picture (always sent, so a rename can move the
 * folder), in one request; a rename also removes the old folder's files (`renamed`: the old name and its picture's
 * name). The dev page reloads when a bundled file changes, so two requests could lose the second.
 */
export async function saveVariant(deviceId: string, variant: string, layout: ComponentLayout, ports: readonly DevicePort[], picture: { readonly name: string; readonly data: Blob }, renamed?: { readonly from: string; readonly image: string }): Promise<void> {
  const files = [
    { path: `${variantDir(variant)}/${picture.name}`, base64: await toBase64(picture.data) },
    { path: layoutFileName(variant), text: serializeLayout(layout, ports) },
  ]
  const remove = renamed ? [layoutFileName(renamed.from), `${variantDir(renamed.from)}/${renamed.image}`] : []
  await check(await fetch(url(deviceId), { method: 'POST', body: JSON.stringify({ files, ...(remove.length ? { remove } : {}) }) }))
}

/** Where the app reads a variant's saved picture (dev server only). */
export const imageUrl = (deviceId: string, variant: string, image: string): string => url(deviceId, `${variantDir(variant)}/${image}`)
