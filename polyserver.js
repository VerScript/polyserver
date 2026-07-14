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
        const headers = { 'User-Agent': 'PolyServer' };
        if (GITHUB_TOKEN) {
            const apiUrl = `https://api.github.com/repos/${GITHUB_ORG}/${repo}/contents/${filePath}`;
            const apiHeaders = {
                'User-Agent': 'PolyServer',
                'Accept': 'application/vnd.github.v3.raw',
                'Authorization': `Bearer ${GITHUB_TOKEN}`
            };
            https.get(apiUrl, { headers: apiHeaders }, (res) => {
                if (res.statusCode === 301 || res.statusCode === 302) {
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
            return;
        }

        const url = `https://raw.githubusercontent.com/${GITHUB_ORG}/${repo}/main/${filePath}`;
        https.get(url, { headers }, (res) => {
            if (res.statusCode === 301 || res.statusCode === 302) {
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
            if (res.statusCode === 200) {
                let data = '';
                res.on('data', chunk => data += chunk);
                res.on('end', () => resolve(data));
            } else {
                const masterUrl = `https://raw.githubusercontent.com/${GITHUB_ORG}/${repo}/master/${filePath}`;
                https.get(masterUrl, { headers }, (mRes) => {
                    if (mRes.statusCode === 200) {
                        let mData = '';
                        mRes.on('data', chunk => mData += chunk);
                        mRes.on('end', () => resolve(mData));
                    } else {
                        reject(new Error(`HTTP ${res.statusCode} (main) & ${mRes.statusCode} (master) for ${repo}/${filePath}`));
                    }
                }).on('error', reject);
            }
        }).on('error', reject);
    });
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


// ─── BINARY FILE FETCHER ────────────────────────────────────────────
function fetchGitHubBinary(repo, filePath, destPath) {
    return new Promise((resolve, reject) => {
        if (GITHUB_TOKEN) {
            const url = `https://api.github.com/repos/${GITHUB_ORG}/${repo}/contents/${filePath}`;
            const headers = {
                'User-Agent': 'PolyServer',
                'Accept': 'application/vnd.github.v3.raw',
                'Authorization': `Bearer ${GITHUB_TOKEN}`
            };
            const followAndSave = (targetUrl, isRedirect = false) => {
                const proto = targetUrl.startsWith('https') ? require('https') : require('http');
                const reqHeaders = isRedirect ? { 'User-Agent': 'PolyServer' } : headers;
                proto.get(targetUrl, { headers: reqHeaders }, (res) => {
                    if (res.statusCode === 301 || res.statusCode === 302) {
                        followAndSave(res.headers.location, true);
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
            return;
        }

        const url = `https://raw.githubusercontent.com/${GITHUB_ORG}/${repo}/main/${filePath}`;
        const headers = { 'User-Agent': 'PolyServer' };
        
        const followAndSavePublic = (targetUrl) => {
            https.get(targetUrl, { headers }, (res) => {
                if (res.statusCode === 301 || res.statusCode === 302) {
                    followAndSavePublic(res.headers.location);
                    return;
                }
                if (res.statusCode !== 200) {
                    const masterUrl = `https://raw.githubusercontent.com/${GITHUB_ORG}/${repo}/master/${filePath}`;
                    https.get(masterUrl, { headers }, (mRes) => {
                        if (mRes.statusCode === 301 || mRes.statusCode === 302) {
                            followAndSavePublic(mRes.headers.location);
                            return;
                        }
                        if (mRes.statusCode !== 200) {
                            reject(new Error(`HTTP ${res.statusCode} (main) & ${mRes.statusCode} (master) for binary ${repo}/${filePath}`));
                            return;
                        }
                        const ws = fs.createWriteStream(destPath);
                        mRes.pipe(ws);
                        ws.on('finish', () => {
                            ws.close();
                            resolve();
                        });
                    }).on('error', reject);
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

        followAndSavePublic(url);
    });
}


// ─── GITHUB REPO FETCHER ────────────────────────────────────────────
function getOrgRepos() {
    return Promise.resolve([
        'VerScript',
        'VS-Sharp',
        'IDE',
        'VerScript.github.io',
        'polyserver'
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
        repos = ['VS-Sharp']; // fallback
    }
    
    // Mount all valid services
    for (const repo of repos) {
        const prefix = `/${repo.toLowerCase()}`;
        const svcDir = path.join(SERVICES_DIR, repo);
        
        console.log(`[PolyServer] Updating and Loading service: ${repo} → ${prefix}`);
        
        try {
            await fetchRepoTarball(repo, svcDir);
            console.log(`  ↓ ${repo} (downloaded latest)`);

        } catch (e) {
            console.warn(`  ✗ Failed to fetch repository ${repo}: ${e.message}`);
        }
        
        // Mount the service if server.js exists
        const serverJsPath = path.join(svcDir, 'server.js');
        if (fs.existsSync(serverJsPath)) {
            try {
                // Clear require cache to ensure fresh module is loaded
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

    console.log('[PolyServer] Fetching VerScript source repository...');
    try {
        const tempSrcDir = path.join(__dirname, 'verscript_src_temp');
        await fetchRepoTarball('VerScript', tempSrcDir);

        // Copy contents carefully instead of rmSync the actual repo directory to avoid Git issues
        const { cpSync } = require('fs');
        cpSync(path.join(tempSrcDir, 'Makefile'), path.join(__dirname, 'verscript_src', 'Makefile'));
        cpSync(path.join(tempSrcDir, 'include'), path.join(__dirname, 'verscript_src', 'include'), { recursive: true, force: true });
        cpSync(path.join(tempSrcDir, 'src'), path.join(__dirname, 'verscript_src', 'src'), { recursive: true, force: true });

        // Cleanup temp dir
        fs.rmSync(tempSrcDir, { recursive: true, force: true });

        console.log('[PolyServer] VerScript source successfully updated.');
    } catch (err) {
        console.warn('[PolyServer] Failed to fetch VerScript source, will attempt to use existing:', err.message);
    }

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
