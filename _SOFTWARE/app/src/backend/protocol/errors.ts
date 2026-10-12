import catalogJson from '@data-structures/errors/errors.generated.json'
import enumsJson from '@data-structures/enums.json'
import type { ErrorNode, ErrorRecord } from './frames'

export interface CatalogField {
  readonly name: string
  readonly type: 'u8' | 'i8' | 'u16' | 'i16' | 'u32' | 'i32' | 'f32' | 'chars'
  readonly offset: number
  readonly size: number
  readonly enum_ref?: string
  readonly id_kind?: string
}

export interface CatalogTag {
  readonly code: number
  readonly tag: number
  readonly name: string
  readonly severity: number
  readonly description?: string
  readonly size: number
  readonly fields: readonly CatalogField[]
}

export interface CatalogModule {
  readonly id: number
  readonly name: string
  readonly tags: readonly CatalogTag[]
}

export type FieldValue = number | string

export interface DescribedNode {
  readonly name: string
  readonly module: string
  readonly severity: number
  readonly description?: string
  /** Decoded payload; an enum field is its member's name, `chars` the text. */
  readonly fields: Readonly<Record<string, FieldValue>>
  /** True when the tag is not in the catalog (a newer firmware): only `tag` and the raw payload are known. */
  readonly unknown: boolean
  readonly tag: number
}

export type EnumMembers = Record<string, { members: readonly { name: string; value: number; alias?: string }[] }>

/** The tags of data-structures/errors/errors.generated.json, looked up by the tag number of an error node. */
export class ErrorCatalog {
  private readonly tags = new Map<number, { module: CatalogModule; tag: CatalogTag }>()
  private readonly enums: EnumMembers

  constructor(
    modules: readonly CatalogModule[] = (catalogJson as unknown as { modules: CatalogModule[] }).modules,
    enums: EnumMembers = (enumsJson as unknown as { enums: EnumMembers }).enums,
  ) {
    for (const module of modules) for (const tag of module.tags) this.tags.set(tag.tag, { module, tag })
    this.enums = enums
  }

  describe(node: ErrorNode): DescribedNode {
    const found = this.tags.get(node.tag)
    if (!found) {
      return { name: `0x${node.tag.toString(16).padStart(8, '0')}`, module: `module ${node.module}`, severity: node.severity, fields: {}, unknown: true, tag: node.tag }
    }
    const view = new DataView(node.payload.buffer, node.payload.byteOffset, node.payload.byteLength)
    const fields: Record<string, FieldValue> = {}
    for (const field of found.tag.fields) {
      if (field.offset + field.size > node.payload.length) break // a shorter payload from an older firmware
      fields[field.name] = this.read(view, node.payload, field)
    }
    return { name: found.tag.name, module: found.module.name, severity: node.severity, description: found.tag.description, fields, unknown: false, tag: node.tag }
  }

  /** The nodes of a record, outermost first. */
  chain(record: ErrorRecord): readonly DescribedNode[] {
    return record.nodes.map((node) => this.describe(node))
  }

  /** The tag name of the root cause (the last node), or undefined for an empty chain. */
  rootCause(record: ErrorRecord): string | undefined {
    const last = record.nodes.at(-1)
    return last ? this.describe(last).name : undefined
  }

  /** `ERR_I2C_REMOTE_DENIED (bus=0)`, or a chain `A > B`: the nodes that carry a failure (not the route), outermost first. */
  format(record: ErrorRecord): string {
    return this.chain(record)
      .filter((node) => node.severity > 0 || node.unknown)
      .map((node) => {
        const values = Object.entries(node.fields).map(([k, v]) => `${k}=${v}`)
        return values.length ? `${node.name} (${values.join(', ')})` : node.name
      })
      .join(' > ')
  }

  private read(view: DataView, bytes: Uint8Array, field: CatalogField): FieldValue {
    const at = field.offset
    let value: number
    switch (field.type) {
      case 'u8': value = view.getUint8(at); break
      case 'i8': value = view.getInt8(at); break
      case 'u16': value = view.getUint16(at, true); break
      case 'i16': value = view.getInt16(at, true); break
      case 'u32': value = view.getUint32(at, true); break
      case 'i32': value = view.getInt32(at, true); break
      case 'f32': value = view.getFloat32(at, true); break
      case 'chars': {
        const text = bytes.subarray(at, at + field.size)
        const end = text.indexOf(0)
        return new TextDecoder().decode(end < 0 ? text : text.subarray(0, end))
      }
    }
    if (field.enum_ref) {
      const member = this.enums[field.enum_ref]?.members.find((m) => m.value === value)
      if (member) return member.name
    }
    return value
  }
}
