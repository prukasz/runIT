import { renderToString } from 'react-dom/server'
import { expect, it } from 'vitest'
import { App } from '../src/App'

it('App renders without throwing', () => {
  expect(renderToString(<App />)).toContain('Main screen')
})
