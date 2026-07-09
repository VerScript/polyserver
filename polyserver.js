// ═══════════════════════════════════════════════════════════════════
//  PolyServer — Unified VerScript Backend
//  Dynamically grabs server modules from VerScript org repos
//  and mounts them under namespaced route prefixes.
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
// Maps repo names to their local module directories.
// On startup, polyserver checks each repo for a server.js that exports
// { mountRoutes: (app, basePath) => void }
// and mounts it under /<repo-name>/...

const SERVICES_DIR = path.join(__dirname, 'services');

// Ensure services directory exists
if (!fs.existsSync(SERVICES_DIR)) {
    fs.mkdirSync(SERVICES_DIR, { recursive: true });
}

// ─── GITHUB FILE FETCHER ────────────────────────────────────────────
function fetchGitHubFile(repo, filePath) {
    return new Promise((resolve, reject) => {
        const url = `https://api.github.com/repos/${GITHUB_ORG}/${repo}/contents/${filePath}`;
        const headers = {
            'User-Agent': 'PolyServer',
            'Accept': 'application/vnd.github.v3.raw'
        };
        if (GITHUB_TOKEN) {
            headers['Authorization'] = `Bearer ${GITHUB_TOKEN}`;
        }
        
        https.get(url, { headers }, (res) => {
            if (res.statusCode === 301 || res.statusCode === 302) {
                // Follow redirect
                https.get(res.headers.location, { headers: { 'User-Agent': 'PolyServer' } }, (rRes) => {
                    let data = '';
                    rRes.on('data', chunk => data += chunk);
                    rRes.on('end', () => {
                        if (rRes.statusCode === 200) resolve(data);
                        else reject(new Error(`HTTP ${rRes.statusCode} for ${repo}/${filePath}`));
                    });
                }).on('error', reject);
                return;
            }
            
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                if (res.statusCode === 200) resolve(data);
                else reject(new Error(`HTTP ${res.statusCode} for ${repo}/${filePath}`));
            });
        }).on('error', reject);
    });
}

// ─── BINARY FILE FETCHER ────────────────────────────────────────────
function fetchGitHubBinary(repo, filePath, destPath) {
    return new Promise((resolve, reject) => {
        const url = `https://api.github.com/repos/${GITHUB_ORG}/${repo}/contents/${filePath}`;
        const headers = {
            'User-Agent': 'PolyServer',
            'Accept': 'application/vnd.github.v3.raw'
        };
        if (GITHUB_TOKEN) headers['Authorization'] = `Bearer ${GITHUB_TOKEN}`;
        
        const followAndSave = (targetUrl) => {
            const proto = targetUrl.startsWith('https') ? https : require('http');
            proto.get(targetUrl, { headers }, (res) => {
                if (res.statusCode === 301 || res.statusCode === 302) {
                    followAndSave(res.headers.location);
                    return;
                }
                if (res.statusCode !== 200) {
                    reject(new Error(`HTTP ${res.statusCode}`));
                    return;
                }
                const ws = fs.createWriteStream(destPath);
                res.pipe(ws);
                ws.on('finish', () => {
                    ws.close();
                    resolve();
                });
            }).on('error', reject);
        };
        
        followAndSave(url);
    });
}

// ─── DYNAMIC SERVICE LOADER ─────────────────────────────────────────
async function loadOrgServices() {
    console.log('[PolyServer] Discovering org services...');
    
    // Known services to mount (repos with server.js that export mountRoutes)
    const serviceRepos = [
        {
            repo: 'VS-Sharp',
            prefix: '/vs-sharp',
            files: ['server.js', 'model_weights.json', 'verscript'],
            dirs: ['knowledge']
        }
    ];
    
    for (const svc of serviceRepos) {
        const svcDir = path.join(SERVICES_DIR, svc.repo);
        if (!fs.existsSync(svcDir)) {
            fs.mkdirSync(svcDir, { recursive: true });
        }
        
        console.log(`[PolyServer] Loading service: ${svc.repo} → ${svc.prefix}`);
        
        // Download each file
        for (const file of svc.files) {
            const destFile = path.join(svcDir, file);
            
            // Skip if already exists (allows bundled files to take precedence)
            if (fs.existsSync(destFile)) {
                console.log(`  ✓ ${file} (bundled)`);
                continue;
            }
            
            try {
                if (file === 'verscript') {
                    // Binary file
                    await fetchGitHubBinary(svc.repo, file, destFile);
                    try { fs.chmodSync(destFile, 0o755); } catch(_) {}
                    console.log(`  ↓ ${file} (binary, downloaded)`);
                } else {
                    const content = await fetchGitHubFile(svc.repo, file);
                    fs.writeFileSync(destFile, content, 'utf8');
                    console.log(`  ↓ ${file} (downloaded)`);
                }
            } catch (err) {
                console.warn(`  ✗ ${file}: ${err.message}`);
            }
        }
        
        // Download directory files
        if (svc.dirs) {
            for (const dir of svc.dirs) {
                const dirPath = path.join(svcDir, dir);
                if (!fs.existsSync(dirPath)) fs.mkdirSync(dirPath, { recursive: true });
                
                try {
                    const dirUrl = `https://api.github.com/repos/${GITHUB_ORG}/${svc.repo}/contents/${dir}`;
                    const dirHeaders = {
                        'User-Agent': 'PolyServer',
                        'Accept': 'application/vnd.github.v3+json'
                    };
                    if (GITHUB_TOKEN) dirHeaders['Authorization'] = `Bearer ${GITHUB_TOKEN}`;
                    
                    const dirContent = await new Promise((resolve, reject) => {
                        https.get(dirUrl, { headers: dirHeaders }, (res) => {
                            let data = '';
                            res.on('data', chunk => data += chunk);
                            res.on('end', () => {
                                if (res.statusCode === 200) resolve(JSON.parse(data));
                                else reject(new Error(`HTTP ${res.statusCode}`));
                            });
                        }).on('error', reject);
                    });
                    
                    for (const item of dirContent) {
                        if (item.type === 'file') {
                            const destFile = path.join(dirPath, item.name);
                            if (!fs.existsSync(destFile)) {
                                const content = await fetchGitHubFile(svc.repo, `${dir}/${item.name}`);
                                fs.writeFileSync(destFile, content, 'utf8');
                                console.log(`  ↓ ${dir}/${item.name}`);
                            } else {
                                console.log(`  ✓ ${dir}/${item.name} (bundled)`);
                            }
                        }
                    }
                } catch (err) {
                    console.warn(`  ✗ ${dir}/: ${err.message}`);
                }
            }
        }
        
        // Mount the service
        try {
            const svcModule = require(path.join(svcDir, 'server.js'));
            if (typeof svcModule.mountRoutes === 'function') {
                svcModule.mountRoutes(app, svc.prefix);
                console.log(`  ✅ Mounted ${svc.repo} at ${svc.prefix}`);
            } else {
                console.warn(`  ⚠️ ${svc.repo}/server.js does not export mountRoutes()`);
            }
        } catch (err) {
            console.error(`  ❌ Failed to mount ${svc.repo}: ${err.message}`);
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
                    hasWeights: fs.existsSync(path.join(svcPath, 'model_weights.json')),
                    hasBinary: fs.existsSync(path.join(svcPath, 'verscript'))
                });
            }
        });
    }
    res.json({
        server: 'PolyServer',
        version: '1.0.0',
        uptime: process.uptime(),
        services
    });
});

// ─── BOOT ───────────────────────────────────────────────────────────
async function boot() {
    const { execSync } = require('child_process');
    try {
        console.log('[PolyServer] Compiling VerScript binary from source...');
        execSync('make -C verscript_src clean && make -C verscript_src && cp verscript_src/verscript ./verscript && chmod +x ./verscript', { stdio: 'inherit' });
        console.log('[PolyServer] Compilation successful!');
    } catch (err) {
        console.warn('[PolyServer] Compilation failed, falling back to precompiled binary:', err.message);
    }

    try {
        await loadOrgServices();
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
