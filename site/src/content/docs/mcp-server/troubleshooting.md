---
title: "Troubleshooting"
description: "Common issues and solutions when using the MCP servers"
---

While we've taken steps to ensure the smoothest experience possible, you may encounter the following issues in the beta release.

### "Could not connect to MCP Server [server name]"

:::tip
**Most likely issue:** Connection to the server timed out.
:::


**Solutions:**

- Check to make sure your configuration aligns with the setup instructions
- Stop generating the current response, refresh the page and try again
- Disconnect from and reconnect to the connector
- If neither of these work, delete the connector and add it again
- If it still doesn't work, contact [jordan.lee@sony.com](mailto:jordan.lee@sony.com) and [report a bug](https://sna-ipsa-developer-tools.atlassian.net/jira/software/form/68c76742-1683-4ac8-83f3-a2e2518e5ffc?atlOrigin=eyJpIjoiMGNmZDVmZmE4MGQyNGZiNGFlZTQ1MGVhYTU2M2JkODciLCJwIjoiaiJ9).

### Claude makes 10+ tool calls before providing an answer.

:::tip
**Most likely issue:** Claude is doing an exhaustive search of the help guides for completeness.
:::


**Solutions:**

- Tell Claude, in your prompt, to refine or narrow its search approach.
- Ask Claude to prioritize quick answers over deep research.
- Interrupt Claude's response and ask it to summarize the information it found.

### "Tool [tool name] not found"

:::tip
**Most likely issue:** Connection to the server was lost. Tools not available for use.
:::


**Solutions:**

- Disconnect from and reconnect to the connector
- If this doesn't work, delete the connector and add it again

### "Claude's response was interrupted"

:::tip
**Most likely issue:** Connection to the server is successful, but connection was temporarily lost due to startup not complete.
:::


**Solutions:**

- Refresh the page, wait 30 seconds before making a query
- Make your first request smaller. After successful response, resume normal operation

### "Tool call [tool name] returned no results or 429 error"

:::tip
**Most likely issue:** Rate limit exceeded (20 requests/second or 100 requests/minute)
:::


**Solutions:**

- Wait several seconds before making another request
- If your LLM is making rapid consecutive requests, prompt it to space out tool calls
- If the tool call body has a `top_k` (number of content to retrieve) greater than 5, prompt it to keep `top_k < 5`:

  ```json
  {
    "query": "exposure compensation",
    //top_k indicates the number of results to return in the response
    "top_k": 1
  }
  ```
