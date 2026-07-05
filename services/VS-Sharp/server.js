// VS-Sharp — Custom Neural LLM + VerScript Runner
// This module exports a function that mounts all routes onto an Express app.
// It is designed to be consumed by PolyServer.

const fs = require('fs');
const path = require('path');
const { exec } = require('child_process');
const os = require('os');

const WEIGHTS_FILE = path.join(__dirname, 'model_weights.json');
const CONTEXT_WINDOW = 3;
const EMBED_DIM = 16;
const HIDDEN_SIZE = 32;

// --- TOKENIZER ---
function tokenize(text) {
    const tokens = [];
    const regex = /(\r?\n|\w+|[^\w\s])/g;
    let match;
    while ((match = regex.exec(text.toLowerCase())) !== null) {
        tokens.push(match[0]);
    }
    return tokens;
}

// --- NEURAL NETWORK FORWARD PASS ---
function forward(contextIdxs, weights) {
    const { E, W1, b1, W2, b2 } = weights;
    const C = contextIdxs.length;
    const D = EMBED_DIM;
    const H = HIDDEN_SIZE;
    const V = b2.length;

    // 1. Concatenate Embeddings
    const x = new Array(C * D);
    for (let c = 0; c < C; c++) {
        const idx = contextIdxs[c];
        const safeIdx = (idx >= 0 && idx < E.length) ? idx : 0;
        const emb = E[safeIdx];
        for (let d = 0; d < D; d++) {
            x[c * D + d] = emb[d];
        }
    }

    // 2. Hidden Layer: h = tanh(x * W1 + b1)
    const h = new Array(H);
    for (let j = 0; j < H; j++) {
        let sum = b1[j];
        for (let i = 0; i < C * D; i++) {
            sum += x[i] * W1[i][j];
        }
        h[j] = Math.tanh(sum);
    }

    // 3. Output Logits: logits = h * W2 + b2
    const logits = new Array(V);
    for (let k = 0; k < V; k++) {
        let sum = b2[k];
        for (let j = 0; j < H; j++) {
            sum += h[j] * W2[j][k];
        }
        logits[k] = sum;
    }

    // 4. Softmax
    const max = Math.max(...logits);
    const exps = logits.map(v => Math.exp(v - max));
    const sumExps = exps.reduce((a, b) => a + b, 0);
    const probs = exps.map(v => v / (sumExps || 1e-10));

    return probs;
}

// --- GENERATE RESPONSE FROM LLM ---
function generateLLMResponse(message, weightsData) {
    const { vocab, weights } = weightsData;
    const vocabMap = new Map(vocab.map((t, idx) => [t, idx]));
    
    const padIdx = vocabMap.get('<pad>');
    const startIdx = vocabMap.get('<start>');
    const sepIdx = vocabMap.get('<sep>');
    const endIdx = vocabMap.get('<end>');
    const unkIdx = vocabMap.get('<unk>');

    const getIdx = t => vocabMap.has(t) ? vocabMap.get(t) : unkIdx;

    // Tokenize prompt
    const promptTokens = tokenize(message);
    const sequenceIdxs = [
        startIdx,
        ...promptTokens.map(getIdx),
        sepIdx
    ];

    const generatedTokens = [];
    const maxGenLength = 200;

    for (let step = 0; step < maxGenLength; step++) {
        // Prepare context
        const context = [];
        for (let c = CONTEXT_WINDOW; c >= 1; c--) {
            const seqIdx = sequenceIdxs.length - c;
            if (seqIdx < 0) {
                context.push(padIdx);
            } else {
                context.push(sequenceIdxs[seqIdx]);
            }
        }

        // Forward pass to get probs
        const probs = forward(context, weights);

        // Softmax sampling with low temperature
        const temp = 0.3;
        const logProbs = probs.map(p => Math.log(p + 1e-10) / temp);
        const maxLog = Math.max(...logProbs);
        const tempExps = logProbs.map(lp => Math.exp(lp - maxLog));
        const tempSum = tempExps.reduce((a, b) => a + b, 0);
        const tempProbs = tempExps.map(te => te / (tempSum || 1e-10));

        // Sample token
        const r = Math.random();
        let cumulative = 0;
        let nextIdx = endIdx;
        for (let i = 0; i < tempProbs.length; i++) {
            cumulative += tempProbs[i];
            if (r <= cumulative) {
                nextIdx = i;
                break;
            }
        }

        if (nextIdx === endIdx) break;

        sequenceIdxs.push(nextIdx);
        generatedTokens.push(vocab[nextIdx]);
    }

    // Decode generated tokens
    let responseText = "";
    generatedTokens.forEach((t) => {
        if (t === '\n') {
            responseText += '\n';
        } else {
            if (responseText.length > 0 && !responseText.endsWith('\n') && t !== '.' && t !== ',' && t !== '!' && t !== '?') {
                responseText += ' ';
            }
            responseText += t;
        }
    });

    return responseText;
}

// --- EXTRACT CODE BLOCK ---
function extractCodeBlock(text) {
    const regex = /```verscript\r?\n([\s\S]*?)```/i;
    const match = text.match(regex);
    if (match) return match[1].trim();
    
    const fallbackRegex = /```\r?\n([\s\S]*?)```/;
    const fallbackMatch = text.match(fallbackRegex);
    return fallbackMatch ? fallbackMatch[1].trim() : null;
}

// --- SMART CODE FIX ---
// Analyzes user code and fixes common VerScript errors
function fixVerScriptCode(code) {
    if (!code || !code.trim()) return null;
    
    const lines = code.split('\n');
    const fixed = lines.map(line => {
        let trimmed = line.trim();
        if (!trimmed || trimmed.startsWith('!')) return line;
        
        // Fix unclosed strings in display statements
        if (trimmed.startsWith('display "') && !trimmed.endsWith('"')) {
            return line + '"';
        }
        
        // Fix missing space after display
        if (trimmed.startsWith('display') && trimmed.length > 7 && trimmed[7] !== ' ') {
            return line.replace('display', 'display ');
        }
        
        return line;
    });
    
    return fixed.join('\n');
}

// --- MOUNT ROUTES ---
// This is the main export. PolyServer calls this to mount VS-Sharp routes.
function mountRoutes(app, basePath) {
    const prefix = basePath || '';
    
    // --- VERSCRIPT CODE RUNNER ---
    const rootBin = path.join(process.cwd(), 'verscript');
    const localBin = path.join(__dirname, 'verscript');
    const VERSCRIPT_BIN = fs.existsSync(rootBin) ? rootBin : localBin;

    app.post(prefix + '/run', async (req, res) => {
        const { code } = req.body;
        if (typeof code !== 'string') {
            return res.status(400).json({ error: 'code (string) is required' });
        }

        const tmpFile = path.join(os.tmpdir(), `vs_${Date.now()}_${Math.random().toString(36).slice(2)}.vrs`);
        try {
            fs.writeFileSync(tmpFile, code, 'utf8');
        } catch (err) {
            return res.status(500).json({ error: 'Failed to write temp file', detail: err.message });
        }

        if (!fs.existsSync(VERSCRIPT_BIN)) {
            try { fs.unlinkSync(tmpFile); } catch(_) {}
            return res.status(500).json({ error: 'VerScript binary not found on server.' });
        }

        // Ensure binary is executable
        try { fs.chmodSync(VERSCRIPT_BIN, 0o755); } catch(_) {}

        exec(`"${VERSCRIPT_BIN}" "${tmpFile}"`, { timeout: 10000 }, (error, stdout, stderr) => {
            try { fs.unlinkSync(tmpFile); } catch (_) {}

            if (error && error.killed) {
                return res.json({ output: stdout || '', error: 'Execution timed out (10s limit).' });
            }

            res.json({
                output: stdout || '',
                error: stderr || (error && !stdout ? error.message : '') || ''
            });
        });
    });

    // --- VS# CHAT API ---
    app.post(prefix + '/api/chat', (req, res) => {
        const { code, message } = req.body;
        
        if (!message) {
            return res.status(400).json({ error: 'Message is required' });
        }

        console.log(`[VS#] Received: "${message}"`);

        // Load weights dynamically
        if (!fs.existsSync(WEIGHTS_FILE)) {
            return res.json({
                response: "### VS# Language Model Initializing\n\nI am currently training my neural network from scratch. Please wait and try again!",
                action: null
            });
        }

        try {
            const weightsData = JSON.parse(fs.readFileSync(WEIGHTS_FILE, 'utf8'));
            let responseText = generateLLMResponse(message, weightsData);
            
            // --- ENHANCED CODE-WRITING LOGIC ---
            const lowerMsg = message.toLowerCase();
            let actionPayload = null;
            
            // Detect "fix" / "correct" / "debug" intent — analyze and fix user's code
            const isFixIntent = /\b(fix|correct|debug|repair|syntax)\b/.test(lowerMsg);
            if (isFixIntent && code && code.trim()) {
                const fixedCode = fixVerScriptCode(code);
                if (fixedCode && fixedCode !== code) {
                    // Append the fixed code block to the LLM response
                    responseText += "\n\n```verscript\n" + fixedCode + "\n```";
                }
            }
            
            // Extract code block from response and set edit action
            const codeBlock = extractCodeBlock(responseText);
            if (codeBlock) {
                actionPayload = {
                    type: "edit",
                    code: codeBlock
                };
            }

            // Simulate typing delay
            setTimeout(() => {
                res.json({
                    response: responseText,
                    action: actionPayload
                });
            }, 400);

        } catch (err) {
            console.error("[VS#] Error generating response:", err);
            res.status(500).json({ error: 'Internal server error running custom LLM.' });
        }
    });
}

module.exports = { mountRoutes };
