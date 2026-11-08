// ═══════════════════════════════════════════════════════════════════
//  PolyServer — Unified VerScript Backend
//  Dynamically mounts server modules under namespaced route prefixes.
//  Optimized for Render 512 MB memory constraints.
// ═══════════════════════════════════════════════════════════════════

const express = require('express');
const cors = require('cors');
const fs = require('fs');
const path = require('path');
const https = require('https');

const app = express();
const PORT = process.env.PORT || 3001;
const GITHUB_TOKEN = process.env.GITHUB_TOKEN || '';
const GITHUB_ORG = 'VerScript';

// ─── CORS ───────────────────────────────────────────────────────────
app.use(cors({
    origin: [
        'https://verscript.github.io',
        /\.onrender\.com$/
    ],
    methods: ['GET', 'POST'],
    allowedHeaders: ['Content-Type']
}));
app.use(express.json({ limit: '1mb' }));

// ─── HEALTH CHECK (the only route that lives in polyserver) ─────────
app.get('/ping', (req, res) => {
    res.send('pong');
});

// ─── SERVICE REGISTRY ───────────────────────────────────────────────
const SERVICES_DIR = path.join(__dirname, 'services');

if (!fs.existsSync(SERVICES_DIR)) {
    fs.mkdirSync(SERVICES_DIR, { recursive: true });
}

// ─── GITHUB TARBALL FETCHER ─────────────────────────────────────────
function fetchRepoTarball(repo, destDir) {
    return new Promise((resolve, reject) => {
        const { spawn } = require('child_process');
        if (fs.existsSync(destDir)) {
            fs.rmSync(destDir, { recursive: true, force: true });
        }
        fs.mkdirSync(destDir, { recursive: true });

        if (GITHUB_TOKEN) {
            const url = `https://api.github.com/repos/${GITHUB_ORG}/${repo}/tarball`;
            const headers = {
                'User-Agent': 'PolyServer',
                'Accept': 'application/vnd.github.v3.raw',
                'Authorization': `Bearer ${GITHUB_TOKEN}`
            };

            const followAndExtract = (targetUrl, isRedirect = false) => {
                const proto = targetUrl.startsWith('https') ? require('https') : require('http');
                const reqHeaders = Object.assign({}, headers);
                if (isRedirect) {
                    delete reqHeaders['Authorization'];
                }

                proto.get(targetUrl, { headers: reqHeaders }, (res) => {
                    if (res.statusCode === 301 || res.statusCode === 302) {
                        followAndExtract(res.headers.location, true);
                        return;
                    }
                    if (res.statusCode !== 200) {
                        reject(new Error(`HTTP ${res.statusCode} for ${repo} tarball`));
                        return;
                    }

                    const tarProcess = spawn('tar', ['-xz', '-C', destDir, '--strip-components=1']);
                    res.pipe(tarProcess.stdin);
                    tarProcess.on('close', (code) => {
                        if (code === 0) resolve();
                        else reject(new Error(`tar process exited with code ${code}`));
                    });
                    tarProcess.on('error', (err) => reject(new Error(`tar process failed: ${err.message}`)));
                }).on('error', reject);
            };

            followAndExtract(url);
            return;
        }

        const publicUrl = `https://github.com/${GITHUB_ORG}/${repo}/archive/refs/heads/main.tar.gz`;
        const headers = { 'User-Agent': 'PolyServer' };

        const followAndExtractPublic = (targetUrl) => {
            https.get(targetUrl, { headers }, (res) => {
                if (res.statusCode === 301 || res.statusCode === 302) {
                    followAndExtractPublic(res.headers.location);
                    return;
                }
                if (res.statusCode !== 200) {
                    const masterUrl = `https://github.com/${GITHUB_ORG}/${repo}/archive/refs/heads/master.tar.gz`;
                    https.get(masterUrl, { headers }, (mRes) => {
                        if (mRes.statusCode === 301 || mRes.statusCode === 302) {
                            followAndExtractPublic(mRes.headers.location);
                            return;
                        }
                        if (mRes.statusCode !== 200) {
                            reject(new Error(`HTTP ${res.statusCode} (main) & ${mRes.statusCode} (master) for ${repo} tarball`));
                            return;
                        }
                        const tarProcess = spawn('tar', ['-xz', '-C', destDir, '--strip-components=1']);
                        mRes.pipe(tarProcess.stdin);
                        tarProcess.on('close', (code) => {
                            if (code === 0) resolve();
                            else reject(new Error(`tar process exited with code ${code}`));
                        });
                        tarProcess.on('error', (err) => reject(new Error(`tar process failed: ${err.message}`)));
                    }).on('error', reject);
                    return;
                }

                const tarProcess = spawn('tar', ['-xz', '-C', destDir, '--strip-components=1']);
                res.pipe(tarProcess.stdin);
                tarProcess.on('close', (code) => {
                    if (code === 0) resolve();
                    else reject(new Error(`tar process exited with code ${code}`));
                });
                tarProcess.on('error', (err) => reject(new Error(`tar process failed: ${err.message}`)));
            }).on('error', reject);
        };

        followAndExtractPublic(publicUrl);
    });
}

// ─── GITHUB REPO FETCHER ────────────────────────────────────────────
// Only mount actual backend services that provide server.js with mountRoutes
function getOrgRepos() {
    return Promise.resolve([
        'VS-Sharp'
    ]);
}

// ─── DYNAMIC SERVICE LOADER ─────────────────────────────────────────
async function loadOrgServices() {
    console.log('[PolyServer] Discovering org services...');
    
    let repos = [];
    try {
        repos = await getOrgRepos();
    } catch (err) {
        console.error('[PolyServer] Failed to fetch org repos:', err.message);
        repos = ['VS-Sharp'];
    }
    
    for (const repo of repos) {
        const prefix = `/${repo.toLowerCase()}`;
        const svcDir = path.join(SERVICES_DIR, repo);
        const serverJsPath = path.join(svcDir, 'server.js');
        
        console.log(`[PolyServer] Updating and Loading service: ${repo} → ${prefix}`);
        
        // If local bundled server.js does not exist, fetch from GitHub
        if (!fs.existsSync(serverJsPath)) {
            try {
                await fetchRepoTarball(repo, svcDir);
                console.log(`  ↓ ${repo} (downloaded latest)`);
            } catch (e) {
                console.warn(`  ✗ Failed to fetch repository ${repo}: ${e.message}`);
            }
        } else {
            console.log(`  ⚡ Using bundled service files for ${repo}`);
        }
        
        // Mount the service if server.js exists
        if (fs.existsSync(serverJsPath)) {
            try {
                delete require.cache[require.resolve(serverJsPath)];
                const svcModule = require(serverJsPath);
                
                if (typeof svcModule.mountRoutes === 'function') {
                    svcModule.mountRoutes(app, prefix);
                    console.log(`  ✅ Mounted ${repo} at ${prefix}`);
                } else {
                    console.warn(`  ⚠️ ${repo}/server.js does not export mountRoutes()`);
                }
            } catch (err) {
                console.error(`  ❌ Failed to mount ${repo}: ${err.message}`);
            }
        }
    }
}

// ─── STATUS ENDPOINT ────────────────────────────────────────────────
app.get('/status', (req, res) => {
    const services = [];
    if (fs.existsSync(SERVICES_DIR)) {
        fs.readdirSync(SERVICES_DIR).forEach(name => {
            const svcPath = path.join(SERVICES_DIR, name);
            if (fs.statSync(svcPath).isDirectory()) {
                services.push({
                    name,
                    hasServer: fs.existsSync(path.join(svcPath, 'server.js')),
                    hasWeights: fs.existsSync(path.join(svcPath, 'model_weights.json')) || fs.existsSync(path.join(svcPath, 'model_weights.bin')),
                    hasBinary: fs.existsSync(path.join(svcPath, 'verscript')) || fs.existsSync(path.join(svcPath, 'verscript.exe'))
                });
            }
        });
    }
    res.json({
        server: 'PolyServer',
        version: '1.0.0',
        uptime: process.uptime(),
        memoryUsage: process.memoryUsage(),
        services
    });
});

// ─── BOOT ───────────────────────────────────────────────────────────
async function boot() {
    const { execSync } = require('child_process');

    // Only update verscript_src if it is missing
    if (!fs.existsSync(path.join(__dirname, 'verscript_src', 'src', 'main.c'))) {
        console.log('[PolyServer] Fetching VerScript source repository...');
        try {
            const tempSrcDir = path.join(__dirname, 'verscript_src_temp');
            await fetchRepoTarball('VerScript', tempSrcDir);

            const { cpSync } = require('fs');
            cpSync(path.join(tempSrcDir, 'Makefile'), path.join(__dirname, 'verscript_src', 'Makefile'));
            cpSync(path.join(tempSrcDir, 'include'), path.join(__dirname, 'verscript_src', 'include'), { recursive: true, force: true });
            cpSync(path.join(tempSrcDir, 'src'), path.join(__dirname, 'verscript_src', 'src'), { recursive: true, force: true });
            if (fs.existsSync(path.join(tempSrcDir, 'stdlib'))) {
                cpSync(path.join(tempSrcDir, 'stdlib'), path.join(__dirname, 'verscript_src', 'stdlib'), { recursive: true, force: true });
            }

            fs.rmSync(tempSrcDir, { recursive: true, force: true });
            console.log('[PolyServer] VerScript source successfully updated.');
        } catch (err) {
            console.warn('[PolyServer] Failed to fetch VerScript source, will attempt to use existing:', err.message);
        }
    }

    try {
        console.log('[PolyServer] Compiling VerScript binary from source...');
        execSync('make -C verscript_src clean && make -C verscript_src && cp verscript_src/verscript ./verscript && chmod +x ./verscript', { stdio: 'inherit' });
        if (fs.existsSync(path.join(__dirname, 'verscript_src', 'stdlib'))) {
            const { cpSync } = require('fs');
            cpSync(path.join(__dirname, 'verscript_src', 'stdlib'), path.join(__dirname, 'stdlib'), { recursive: true, force: true });
        }
        console.log('[PolyServer] Compilation successful!');
    } catch (err) {
        console.warn('[PolyServer] Compilation failed, falling back to precompiled binary:', err.message);
    }

    try {
        await loadOrgServices();

        // Propagate compiled verscript binary and stdlib to VS-Sharp service directory
        const vsSharpDir = path.join(SERVICES_DIR, 'VS-Sharp');
        if (fs.existsSync(vsSharpDir)) {
            const binSource = fs.existsSync(path.join(__dirname, 'verscript'))
                ? path.join(__dirname, 'verscript')
                : (fs.existsSync(path.join(__dirname, 'verscript.exe')) ? path.join(__dirname, 'verscript.exe') : null);

            if (binSource) {
                try {
                    const destBin = path.join(vsSharpDir, path.basename(binSource));
                    fs.copyFileSync(binSource, destBin);
                    try { fs.chmodSync(destBin, 0o755); } catch (_) {}
                } catch (cpErr) {
                    console.warn('[PolyServer] Could not copy binary to VS-Sharp:', cpErr.message);
                }
            }
            if (fs.existsSync(path.join(__dirname, 'stdlib'))) {
                try {
                    const { cpSync } = require('fs');
                    cpSync(path.join(__dirname, 'stdlib'), path.join(vsSharpDir, 'stdlib'), { recursive: true, force: true });
                } catch (cpErr) {
                    console.warn('[PolyServer] Could not copy stdlib to VS-Sharp:', cpErr.message);
                }
            }
            console.log('[PolyServer] Successfully synced latest verscript binary and stdlib to VS-Sharp.');
        }
    } catch (err) {
        console.error('[PolyServer] Service loading error:', err.message);
    }
    
    app.listen(PORT, () => {
        console.log(`\n[PolyServer] Running on port ${PORT}`);
        console.log(`[PolyServer] Health: http://localhost:${PORT}/ping`);
        console.log(`[PolyServer] Status: http://localhost:${PORT}/status`);
    });
}

boot();
