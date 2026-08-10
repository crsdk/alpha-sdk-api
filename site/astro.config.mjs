// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Project Pages serve from https://<owner>.github.io/alpha-sdk-api, so the
// whole site is mounted under a path prefix. `base` is the repo name, which the
// deploy guard in .github/workflows/pages.yml also keys on — an ownership
// transfer changes the owner, never the repo name, so this stays correct.
const base = '/alpha-sdk-api';

// Sidebar order mirrors docs.json's navigation groups exactly, rather than
// autogenerating. Autogeneration would nest sdk/recipes under SDK Reference,
// where docs.json had Examples as its own group, and would order pages
// alphabetically instead of by reading order.
export default defineConfig({
  site: 'https://crsdk.github.io',
  base,
  integrations: [
    starlight({
      title: 'Alpha Camera REST API',
      description:
        'An HTTP server that runs on your machine and controls a connected Sony Alpha camera.',
      favicon: '/assets/favicon.png',
      customCss: ['./src/styles/custom.css'],
      components: {
        // Injected into every page's <head>; see the component for why the
        // verb tagging happens at runtime rather than in the content.
        Head: './src/components/Head.astro',
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/crsdk/alpha-sdk-api',
        },
      ],
      // Starlight ships Pagefind, built at deploy time. This is what replaces
      // Mintlify's built-in search, which had no config to port.
      pagefind: true,
      sidebar: [
        {
          // Starlight prepends `base` to sidebar links — repeating it here
          // produces /alpha-sdk-api/alpha-sdk-api/api-reference/.
          label: 'API reference & request console',
          link: '/api-reference/',
          attrs: { target: '_self' },
        },
        { label: 'Platform overview', slug: 'platform-overview' },
        {
          label: 'REST API',
          items: [
            { slug: 'web-api/overview' },
            { slug: 'web-api/server' },
            { slug: 'web-api/compatibility' },
            { slug: 'web-api/connection' },
            { slug: 'web-api/properties' },
            { slug: 'web-api/actions' },
            { slug: 'web-api/live-view' },
            { slug: 'web-api/events' },
            { slug: 'web-api/advanced-topics' },
            { slug: 'web-api/sd-card' },
            { slug: 'web-api/settings' },
          ],
        },
        {
          label: 'Clients',
          items: [{ slug: 'sdk/overview' }],
        },
        {
          label: 'Examples',
          items: [
            { slug: 'sdk/recipes/sse-events' },
            { slug: 'sdk/recipes/live-view-polling' },
            { slug: 'sdk/recipes/server-subprocess' },
            { slug: 'sdk/recipes/discovery-reconnect' },
            { slug: 'sdk/recipes/retry-backoff' },
            { slug: 'sdk/recipes/react-hook' },
          ],
        },
        {
          label: 'MCP Server',
          items: [
            { slug: 'mcp-server/overview' },
            { slug: 'mcp-server/setup' },
            { slug: 'mcp-server/camera-control' },
            { slug: 'mcp-server/system-prompt' },
            { slug: 'mcp-server/troubleshooting' },
          ],
        },
        {
          label: 'Contributing',
          items: [
            { slug: 'contributing' },
            { slug: 'contributing/adding-endpoints' },
            { slug: 'contributing/agent-skills' },
          ],
        },
        {
          label: 'Changelog',
          items: [
            { slug: 'changelog/05-04-2026' },
            { slug: 'changelog/04-03-2026' },
            { slug: 'changelog/03-29-2026' },
            { slug: 'changelog/03-25-2026' },
            { slug: 'changelog/03-17-2026' },
            { slug: 'changelog/02-27-2026' },
          ],
        },
      ],
    }),
  ],
});
