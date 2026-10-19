import type { Scope } from './scopes'

/** The scopes (world layer, behind the wires and the blocks): a tinted area per branch or loop body, named at its top; the outer ones first. */
export function CanvasScopes({ scopes }: { scopes: readonly Scope[] }) {
  return (
    <>
      {[...scopes].sort((a, b) => a.depth - b.depth).map((scope) => (
        <div key={scope.key} className={`canvas-scope hue-${scope.hue}`} aria-hidden="true"
          style={{ left: scope.rect.x, top: scope.rect.y, width: scope.rect.width, height: scope.rect.height }}>
          <span className="canvas-scope-label">{scope.label}</span>
        </div>
      ))}
    </>
  )
}
