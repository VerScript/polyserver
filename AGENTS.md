# AGENTS.md — VerScript PolyServer

Instructions and context for autonomous AI coding agents (such as Google Jules).

## 🚀 Dev Environment & Commands
- **Install Dependencies**: `npm install`
- **Start Gateway Server**: `npm start` or `node polyserver.js`
- **Health Check Endpoint**: `GET /ping` (returns `pong`)
- **Status/Service Discovery Endpoint**: `GET /status`

## 🏗️ Architecture & Project Structure
- `polyserver.js`: Main gateway server. Dynamically mounts sub-services from the `services/` directory.
- `services/VS-Sharp/`: Local bundled service files for the VS-Sharp LLM and VerScript code runner.
- `verscript_src/`: C source code of the VerScript interpreter.
- `verscript`: Precompiled/compiled binary executed by the VS-Sharp code runner endpoint.

## ⚙️ Compilation Rules
- **Boot Compilation**: PolyServer programmatically compiles the C interpreter on startup (`make -C verscript_src clean && make -C verscript_src`) and outputs the binary to `./verscript`.
- **Do NOT remove** the compilation step inside the `boot()` function of `polyserver.js`. This is critical to prevent platform architecture mismatches on deployment.

## 🛡️ Coding Guidelines & Rules
- **CORS Configuration**: Restrict allowed CORS origins strictly to `https://verscript.github.io` and `*.onrender.com` domains.
- **Port Handling**: Always use `process.env.PORT || 3001` for Express server listening.
- **Routing Namespace**: Ensure all sub-services are namespaced (e.g. `/vs-sharp` for VS-Sharp endpoints).
- **Asynchronous Execution**: Keep child processes and endpoints resilient to timeouts.
