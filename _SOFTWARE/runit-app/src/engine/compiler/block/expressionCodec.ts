import type { Catalog } from '../../catalog/catalog'
import { expressionLanguage } from '../../expression'
import type { EnumTables } from '../../generated/wire'
import type { BlockCodec } from './codecs'

/**
 * The codecs of the formula editors: every editor of a catalog block that declares `opcodes`. The opcodes come from the
 * language of the block being encoded. What the editor leaves on the block is its text: `instance.editor = { text: 'in0 * 2 + in1' }`.
 */
export const expressionCodecs = (catalog: Catalog, enums: EnumTables): readonly BlockCodec[] =>
  [...new Set(catalog.blocks.flatMap((b) => (b.editor && b.opcodes ? [b.editor] : [])))].map(
    (editor): BlockCodec => ({
      editor,
      encode: ({ type, instance, pins, report }) => {
        const language = expressionLanguage(enums, type)
        const text = (instance.editor as { text?: unknown } | undefined)?.text
        if (!language || typeof text !== 'string') {
          report('error', 'expression-empty', 'The block has no formula.')
          return null
        }
        const result = language.compile(text, pins.inputs.length, (pin) => instance.inputs[pin] != null)
        if (!result.ok) {
          report('error', 'expression-error', `${result.error.message} (characters ${result.error.from + 1}-${result.error.to})`)
          return null
        }
        return language.state(result.formula)
      },
    }),
  )
