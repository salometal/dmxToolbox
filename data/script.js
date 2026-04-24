/* ==========================================================================
   1. VARIABILI DI STATO E INIZIALIZZAZIONE
   ========================================================================== */
let currentCmd = "";
let isCheckMode = false;
let isRecMode = false;
let keypadLog = []; // Inizializzato per sicurezza
let currentSubnet = "255.255.255.0";
let currentUniverse = 0;
let currentRefresh = 40;
let currentIP = "0.0.0.0";
let isScanning = false;
let currentMacroMode = 'RUN'; // 'RUN' o 'SAVE'
let macroNames = []; // Verrà popolata dal C++ (settings.macros)
let currentSnapMode = 'LIST';
let groupStart = 0;
let groupEnd = 0;
let groupStep = 1;
let lastFaderVal = 0;

document.addEventListener('touchmove', function(e) {
    if (e.touches.length > 1) e.preventDefault();
}, { passive: false });
document.addEventListener('gesturestart', function(e) {
    e.preventDefault();
});
document.addEventListener('gesturechange', function(e) {
    e.preventDefault();
});

document.addEventListener('DOMContentLoaded', () => {
    console.log("DOM Caricato. Inizializzazione...");
    updateStatus();
    setInterval(updateStatus, 3000);
    
    // Inizializzazione interfacce
    toggleIPFields();
    autoPopulateWifi();
    loadPresetsFromESP();
    loadSetup();



    const display = document.getElementById('cmd-display');
    const toggle = document.getElementById('keypadToggle');
    
    if (display && toggle) {
        if (!toggle.checked) {
            display.innerText = "OFF";
            display.style.color = "#ff4444";
            display.style.opacity = "0.6";
        }
    }

});

/* ==========================================================================
   2. CORE: AGGIORNAMENTO STATO E NAVIGAZIONE UI
   ========================================================================== */
/* ==========================================================================
   2. CORE: AGGIORNAMENTO STATO E NAVIGAZIONE UI (AGGIORNATO)
   ========================================================================== */
function updateStatus() {
    if (isScanning) return;

    fetch('/status')
        .then(r => r.json())
        .then(d => {
            currentIP        = d.ip;
            currentUniverse  = d.universe;
            currentRefresh   = String(d.refresh);
            currentSubnet    = d.subnet;

            const ssid           = d.ssid;
            const mode           = d.mode;
            const run            = d.running;
            const tx             = d.unicast;
            const targetIP       = d.targetIp;
            const savedHostname  = d.hostname;
            const keypadActive   = d.keypad;
            const artnetConfirmed = d.artnet;
            const sceneActive    = d.scene;
            const blackoutActive = d.blackout;


            // Aggiorna stato live per modal scene
            isLiveForSnap = d.running || d.scene || d.keypad;
            if (typeof settings === 'undefined') window.settings = {};
            settings._running = d.running;

            // Macro
            if (d.macros) {
                if (typeof settings === 'undefined') window.settings = {};
                settings.macros = d.macros;
                const modal = document.getElementById('macro-modal');
                if (modal && modal.style.display === 'flex') renderMacroGrid();
            }

            // Scene
            if (d.scenes) {
                if (typeof settings === 'undefined') window.settings = {};
                settings.snaps = d.scenes;
                const snapModal = document.getElementById('snap-modal');
                if (snapModal && snapModal.style.display === 'flex') renderSnapGrid();
            }

            // Sync keypad toggle
            const toggle = document.getElementById('keypadToggle');
            if (toggle && toggle.checked !== keypadActive) {
                toggle.checked = keypadActive;
                const display = document.getElementById('cmd-display');
                if (display) {
                    if (!keypadActive) {
                        display.innerText = "OFF";
                        display.style.color = "#ff4444";
                        display.style.opacity = "0.6";
                    } else {
                        display.innerText = "CHAN _";
                        display.style.color = "";
                        display.style.opacity = "1";
                    }
                }
            }

            // Badge modo
            const modeBadge = document.getElementById("mode-badge");
            const modes = ["DMX -> ARTNET", "ARTNET -> DMX", "STANDALONE"];

            if (blackoutActive) {
                modeBadge.innerText = "⬛ BLACKOUT";
                modeBadge.style.backgroundColor = "#000";
                modeBadge.style.border = "1px solid #ff4444";
                modeBadge.style.cursor = "pointer";
                modeBadge.onclick = releaseScene;
            } else if (sceneActive) {
                modeBadge.innerText = "🎬 SCENE STOP";
                modeBadge.style.backgroundColor = "#ff4444";
                modeBadge.style.cursor = "pointer";
                modeBadge.style.border = "";
                modeBadge.onclick = releaseScene;
            } else if (keypadActive) {
                modeBadge.innerText = "KEYPAD";
                modeBadge.style.border = "";
                modeBadge.style.backgroundColor = "#4b48ee";
                modeBadge.onclick = null;
            } else if (!run) {
                modeBadge.innerText = "NODO: NON ATTIVO";
                modeBadge.style.border = "";
                modeBadge.style.backgroundColor = "#6c757d";
                modeBadge.onclick = null;
            } else if (mode === 1 && run && !artnetConfirmed) {
                modeBadge.innerText = "🔍 RICERCA SEGNALE...";
                modeBadge.style.backgroundColor = "#ff9800";
                modeBadge.style.border = "";
                modeBadge.onclick = null;
            } else {
                modeBadge.innerText = "LIVE: " + (modes[mode] || "IDLE");
                modeBadge.style.border = "";
                modeBadge.style.backgroundColor = "var(--success)";
                modeBadge.onclick = null;
            }

            // Refresh select
            const refreshSelects = document.querySelectorAll('select[name="refresh"]');
            if (currentRefresh && currentRefresh !== "0") {
                refreshSelects.forEach(select => {
                    if (document.activeElement !== select) {
                        const optionExists = Array.from(select.options).some(opt => opt.value === currentRefresh);
                        if (optionExists && select.value !== currentRefresh) select.value = currentRefresh;
                    }
                });
            }

            // IP e hostname
            const currentIpDisplay = document.getElementById("current-ip");
            if (currentIpDisplay) currentIpDisplay.innerText = currentIP;

            const hostInput = document.getElementById('hostname-input');
            if (hostInput && document.activeElement !== hostInput) {
                hostInput.value = savedHostname || "dmxtoolbox";
                validateHostname(hostInput);
            }

            // Badge header
            document.getElementById("ssid-badge").innerText = "WIFI: " + ssid;
            const txBadge = document.getElementById("tx-badge");
            if (txBadge) {
                if (tx) {
                    txBadge.innerText = "UCAST: " + targetIP;
                    txBadge.style.backgroundColor = "#ffc107";
                    txBadge.style.color = "#000";
                } else {
                    txBadge.innerText = "BCAST";
                    txBadge.style.backgroundColor = "#6c757d";
                    txBadge.style.color = "";
                }
            }

            // Status interni pannelli
            const s1 = document.getElementById("status-text-1");
            const s2 = document.getElementById("status-text-2");
            if (s1) { s1.innerText = (mode === 0 && run) ? "ATTIVO" : "IDLE"; s1.style.color = (mode === 0 && run) ? "var(--success)" : ""; }
            if (s2) { s2.innerText = (mode === 1 && run) ? "ATTIVO" : "IDLE"; s2.style.color = (mode === 1 && run) ? "var(--success)" : ""; }

            // Pannello sistema
            const valIp = document.getElementById("val-ip");
            if (valIp) valIp.innerText = currentIP;
            const valUniverse = document.getElementById("val-universe");
            if (valUniverse) valUniverse.innerText = currentUniverse;
            const valHz = document.getElementById("val-hz");
            if (valHz) valHz.innerText = currentRefresh + " Hz";
            const valUp = document.getElementById("val-up");
            if (valUp && d.uptime) valUp.innerText = formatUptime(d.uptime);
            const valFs = document.getElementById("val-fs");
            if (valFs) valFs.innerText = d.fs + "% Utilizzata";
            const modeDisplay = document.getElementById("val-mode");
            if (modeDisplay) {
                if (keypadActive) modeDisplay.innerText = "KEYPAD";
                else if (!run) modeDisplay.innerText = "STANDBY";
                else { const modeLabels = ["DMX→ART", "ART→DMX", "STANDALONE"]; modeDisplay.innerText = modeLabels[mode] || "--"; }
            }

            // Pulsanti controllo
            const btnDmx = document.getElementById("btn-ctrl-dmxin");
            const btnArtNet = document.getElementById("btn-ctrl-artnetin");
            if (btnDmx) { btnDmx.innerText = "AVVIA TRASMISSIONE"; btnDmx.classList.remove("btn-danger"); }
            if (btnArtNet) { btnArtNet.innerText = "AVVIA RICEZIONE"; btnArtNet.classList.remove("btn-danger"); }
            if (run && !keypadActive) {
                if (mode === 0 && btnDmx) { btnDmx.innerText = "FERMA TRASMISSIONE"; btnDmx.classList.add("btn-danger"); }
                else if (mode === 1 && btnArtNet) {
                    if (!artnetConfirmed) {
                        btnArtNet.innerHTML = '<span class="spinner"></span> RICERCA SEGNALE... (Ferma)';
                        btnArtNet.classList.add("btn-searching");
                    } else {
                        btnArtNet.innerText = "FERMA RICEZIONE";
                        btnArtNet.classList.remove("btn-searching");
                    }
                    btnArtNet.classList.add("btn-danger");
                }
            }

        })
        .catch(e => console.error("Errore status:", e));
}



function showTab(id) {
    document.querySelectorAll('.card').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('nav button').forEach(b => b.classList.remove('active'));
    const target = document.getElementById(id);
    if (target) target.classList.add('active');
    
    const btnMap = { 
        'network': 'btn-network', 
        'pane-dmx-in': 'btn-dmx-in', 
        'pane-artnet-in': 'btn-artnet-out', 
        'pane-standalone': 'btn-standalone' 
    };   
    const btn = document.getElementById(btnMap[id]);
    if (btn) btn.classList.add('active');
}




function toggleCheck() {
    isCheckMode = !isCheckMode;
    const btn = document.getElementById('btn-check-mode');
    if (isCheckMode) btn.classList.add('active');
    else btn.classList.remove('active');
}

function toggleRec() {
    // 1. Controlliamo se il keypad è attivo (fondamentale per salvare il suo buffer)
    const keypadToggle = document.getElementById('keypadToggle');
    if (keypadToggle && !keypadToggle.checked) {
        alert("Attiva il KEYPAD prima di registrare una Macro!");
        return;
    }

    // 2. Apriamo il modal in modalità SAVE
    // Questo attiverà visivamente il tasto REC
    openMacroModal('SAVE');
}

function toggleEngine(mode, run) {
    fetch(`/toggle?m=${mode}&run=${run ? 1 : 0}`).then(() => {
        setTimeout(updateStatus, 500);
    });
}



/* ==========================================================================
   5. CONFIGURAZIONE WIFI E ART-NET
   ========================================================================== */
function autoPopulateWifi() {
    fetch('/wifi_list')
        .then(r => r.text())
        .then(data => {
            const dropdown = document.getElementById('wifi-dropdown');
            if (!dropdown || !data || data === "NONE") {
                if(dropdown) dropdown.innerHTML = '<option value="">Nessuna rete trovata</option>';
                return;
            }
            dropdown.innerHTML = '<option value="">-- Seleziona Rete --</option>';
            data.split(",").forEach(net => {
                const info = net.split("|");
                if (info[0]) {
                    const opt = document.createElement('option');
                    opt.value = info[0];
                    opt.text = info[0] + " (" + (info[1] || "??") + " dBm)";
                    dropdown.add(opt);
                }
            });
        })
        .catch(err => console.error("Errore WiFi:", err));
}

function selectNetwork(val) {
    if (val) document.getElementById('ssid-input').value = val;
}

async function scanArtNet() {
    const modal = document.getElementById('artnet-modal');
    const list = document.getElementById('artnet-nodes-list');
    
    // Mostra il modal e svuota la lista con uno spinner
    modal.style.display = 'flex';
list.innerHTML = `
    <div style="text-align:center; padding:40px;">
        <span class="spinner" style="border: 2px solid rgba(0, 212, 255, 0.2) !important; border-top-color: #00d4ff !important; display:block; margin: 0 auto 15px auto;"></span>
        <div style="color:#00d4ff; font-size:0.8rem; letter-spacing:2px;">
            SCANSIONE IN CORSO...
        </div>
    </div>`;

    try {
        const response = await fetch('/discover');
        const nodes = await response.json();
        
        // Recuperiamo il nostro IP attuale (già presente nella UI)
        const myIp = document.getElementById('current-ip').innerText;

        // Filtriamo i nodi per categorie
        const family = nodes.filter(n => n.type === 'family');
        const artnet = nodes.filter(n => n.type === 'artnet');

        let html = `
            <div style="padding:10px; background:#222; border-radius:5px; margin-bottom:15px; border-left:4px solid var(--accent);">
                <div style="display:flex; justify-content:space-between; align-items:center; margin-top:5px;">
                    <span style="color:var(--accent); "><strong>${myIp}</strong> (Tu)</span>
                </div>
            </div>
            <hr style="border:0; border-top:1px solid #00d4ff; margin:15px 0;">
        `;

       

        

        // Sezione FAMIGLIA (mDNS)
        html += '<small style="color:var(--accent); font-weight:bold; display:block; margin-bottom:10px;">I TUOI DMXTOOLBOX</small>';
        if (family.length > 0) {
            family.forEach(node => {
                html += renderNodeRow(node, '🟠');
            });
        } else {
            html += '<p style="font-size:0.8rem; color:#666;">Nessun altro DMXToolbox rilevato</p>';
        }

       



 // Sezione ART-NET
 
        
        if (artnet.length > 0) {
            html += '<hr style="border:0; border-top:1px solid #00d4ff; margin:15px 0;">';
            html += '<small style="color:#555; font-weight:bold; display:block; margin-bottom:10px; text-transform:uppercase; letter-spacing:1px;">Ecosistema Art-Net</small>';
            artnet.forEach(node => {
                html += renderNodeRow(node, '🔵');
            });
        } else {
            html += '<p style="font-size:0.8rem; color:#666;">Nessun nodo Art-Net trovato</p>';
        }
    
        list.innerHTML = html;

    } catch (e) {
        list.innerHTML = '<div style="color:var(--danger); padding:20px;">Errore durante la scansione.</div>';
    }

}
// Funzione di supporto per creare la riga del nodo
function renderNodeRow(node, emoji) {
    return `
        <div class="node-item" style="display:flex; justify-content:space-between; align-items:center; padding:10px 0; border-bottom:1px solid #333;">
            <div>
                <div style="font-weight:bold;">${emoji} ${node.name}</div>
                <div style="font-size:0.8rem; color:#aaa;">${node.ip}</div>
            </div>
            <span style="padding:0 3% 0 0 !important;"><button onclick="setUnicastTarget('${node.ip}')" class="PlayButton"> ▶ </button></span>
        </div>
    `;
}

// Funzione per impostare l'IP e attivare l'Unicast
function setUnicastTarget(ip) {
    const uniFlag = document.getElementById('unicast-flag');
    const targetInput = document.getElementById('target-ip');
    
    // 1. Imposta l'IP
    targetInput.value = ip;
    
    // 2. Attiva la checkbox Unicast se spenta
    if (!uniFlag.checked) {
        uniFlag.checked = true;
        // Chiamiamo la funzione che mostra il campo IP (quella che hai già nell'HTML)
        toggleUnicastField();
    }
    
    // 3. Chiudi il modal
    document.getElementById('artnet-modal').style.display = 'none';
    
    // 4. Feedback visivo
    console.log("Target Unicast impostato su: " + ip);
}


    


/* ==========================================================================
   6. UTILITY E GESTIONE FORM
   ========================================================================== */
   
   function toggleHeader() {
    const header = document.getElementById('main-header');
    header.classList.toggle('expanded');
}

function toggleIPFields() {
    const flag = document.getElementById('dhcp-flag');
    const fields = document.getElementById('static-fields');
    if (flag && fields) fields.style.display = flag.checked ? 'none' : 'block';
}

function toggleUnicastField() {
    const flag = document.getElementById('unicast-flag');
    const group = document.getElementById('unicast-group');
    if (flag && group) group.style.display = flag.checked ? 'block' : 'none';
}

function handleForm(event, form) {
event.preventDefault();
    
    const action = form.getAttribute("action");
    const params = new URLSearchParams(new FormData(form));
    
    // Controlliamo lo stato attuale dai badge/pulsanti
    const modeBadge = document.getElementById("mode-badge");
    const isLive = modeBadge ? modeBadge.innerText.includes("KEYPAD") : false;
    const modeBadgeText = modeBadge ? modeBadge.innerText : "";

    // 1. Prendi i riferimenti agli elementi una sola volta
    const hInput = document.getElementById('hostname-input');
    const ssidField = document.getElementById("ssid");
    const passField = document.getElementById("pass");

    // 2. Estrai i valori reali (trim rimuove spazi bianchi accidentali)
    const hVal = hInput ? hInput.value.trim() : "";
    const ssidVal = ssidField ? ssidField.value.trim() : "";
    const passVal = passField ? passField.value.trim() : "";


    // AGGIUNGI Hostname 
    if (hInput) params.set("h", hInput.value);

    // AGGIUNGI refresh rate (con controllo sicurezza)
    const refreshSelect = form.querySelector('select[name="refresh"]');
    if (refreshSelect) {
        params.set("r", refreshSelect.value); // Forza 'r' per il C++
    }


 

if (action === "/connect") {


const pwdField = document.getElementById("pwd");
    const pwdVal = pwdField ? pwdField.value.trim() : "";

    if (pwdVal === "") {
        // --- CASO VUOTO: CAMBIO IDENTITÀ ---
        // Recuperiamo l'hostname dal tuo hInput o dai params
        const hVal = params.get("h") || "";
        
        fetch("/set-hostname?h=" + encodeURIComponent(hVal))
// Mostro il messaggio 
alert("Configurazione completata.\n\n" +
      "Il dispositivo si sta riavviando per applicare la nuova configurazione.\n" +
      "La pagina verrà reindirizzata automaticamente tra 5 secondi a:\n" + 
      "http://" + hVal.toLowerCase() + ".local");// 3. Reindirizziamo l'utente dopo un breve delay
    setTimeout(() => {
        window.location.href = "http://" + hVal.toLowerCase() + ".local";
    }, 5000);
    } 
    else {

if (!confirm("Il dispositivo si riavvierà. Procedere?")) return;
       const hInput = document.getElementById('hostname-input');
        if (hInput) params.set("h", hInput.value.trim());

        // Sincronizziamo la password (usando il tuo nuovo id 'pwd')
     params.set("s", document.getElementById("ssid-input").value);
    params.set("p", pwdVal);
    params.set("h", document.getElementById("hostname-input").value);
        
        // La fetch invierà sia 'pass' che 'h' (e 'ssid') in un colpo solo
        fetch(action + "?" + params.toString()).then(r => r.text()).then(msg => {
            alert(msg);
            location.reload();
        });
         


    }
       
    } 



    
    else if (action === "/artnetin" || action === "/dmxin") {
        // Determiniamo se stiamo avviando o fermando


      const submitBtn = form.querySelector('button[type="submit"]');
    const btnText = submitBtn ? submitBtn.innerText.toUpperCase() : "";

    // 2. La logica di STOP è ora elementare:
    // Se il tasto che ho premuto dice "FERMA" o Ricerca, allora invio run=0.

const isStopping = btnText.includes("FERMA") || 
                   btnText.includes("RICERCA");

        
        

        if (isStopping) {
            // Se stiamo fermando, aggiungiamo run=0 ai parametri
            params.set("run", "0");
            fetch(action + "?" + params.toString()).then(() => {
                const btnArtNet = document.getElementById("btn-ctrl-artnetin");
                    if (btnArtNet) {
                        btnArtNet.classList.remove("btn-searching");
                        btnArtNet.disabled = false;
                    }
                alert("Operazione fermata.");
                updateStatus(); // Aggiorna subito i badge
            });
        } else {
            // --- CONTROLLO UNICAST (Solo per DMX IN) ---
     if (action === "/dmxin") {
                const uniFlag = document.getElementById('unicast-flag');
                const targetInput = document.getElementById('target-ip'); // ID del tuo campo IP unico

                params.set("u_uni", uniFlag && uniFlag.checked ? "1" : "0"); // <-- AGGIUNTO: Fondamentale per il C++
                
               if (uniFlag && uniFlag.checked) { // <-- AGGIUNTO: Controlliamo IP solo se Unicast è attivo
                    const targetIP = targetInput.value;
                    const deviceIP = currentIP; 

                    if (targetIP === "0.0.0.0" || targetIP === "" || targetIP === "undefined") {
                        alert("Errore: Inserisci un IP di destinazione valido.");
                        return;
                    }

                    if (!isIpInSubnet(targetIP, deviceIP, currentSubnet)) {
                        if (!confirm("L'IP di destinazione (" + targetIP + ") sembra fuori dalla tua sottorete. Vuoi procedere comunque?")) {
                            return;
                        }
                    }
                }
            }

        
            
// --- LOGICA DI AVVIO ---
const label = (action === "/dmxin") ? "DMX IN -> Art-Net OUT" : "Art-Net IN -> DMX OUT";
if (action === "/dmxin") {
    if (!confirm("Vuoi avviare la modalità " + label + "?")) return;
}
            fetch('/release_snap'); // ← manda snap release 
            params.set("run", "1");


            // Feedback visivo specifico per Art-Net IN (sniffer)
            const btnArtNet = document.getElementById("btn-ctrl-artnetin");
            if (action === "/artnetin" && btnArtNet) {
                
                btnArtNet.innerHTML = '<span class="spinner"></span> RICERCA SEGNALE...';
                btnArtNet.classList.add("btn-searching");
               
            }

            console.log("URL finale:", action + "?" + params.toString()); 

            fetch(action + "?" + params.toString())
                .then(response => response.text()) // Aspettiamo la risposta dall'ESP (OK_START / ERR_NO_DATA)
                .then(responseText => {
                    if (action === "/artnetin") {
                        // Nessun alert — il badge si aggiorna da solo via updateStatus
                        console.log("[ArtNet] Risposta ESP:", responseText);
                    } 
                })
                .catch(err => {
                    console.error("Errore fetch:", err);
                    alert("Errore di connessione con l'ESP32.");
                })
                .finally(() => {
                    isScanning = false;
                    if (btnArtNet) {
                            btnArtNet.classList.remove("btn-searching");
                            btnArtNet.disabled = false;
                            // Usiamo updateStatus() per rimettere il testo corretto (ATTIVA o FERMA)
                            // basandoci sullo stato reale ritornato dall'ESP32
                            updateStatus(); 
                    }
                });
        }
    }
} // <--- Fine della funzione handleForm

function isIpInSubnet(targetIP, deviceIP, subnet) {
    // Trasforma stringa IP in numero a 32 bit
    const ipToNum = ip => ip.split('.').reduce((acc, octet) => (acc << 8) + parseInt(octet, 10), 0) >>> 0;
    
    const target = ipToNum(targetIP);
    const device = ipToNum(deviceIP);
    const mask = ipToNum(subnet);

    // Se il bitwise AND tra IP e Subnet è uguale, sono nello stesso pool
    return (target & mask) === (device & mask);
}
function validateHostname(input) {
    // Pulisce l'input istantaneamente (UX di prevenzione errori)
    input.value = input.value.toLowerCase().replace(/[^a-z0-9-]/g, '');
    
    // Aggiorna l'anteprima "Dopo il riavvio"
    const preview = document.getElementById('hostname-preview');
    const name = input.value || "dmxtoolbox";
    preview.innerText = `http://${name}.local`;
}
/* ==========================================================================
   4. GESTIONE MACRO LOG (STORICO COMANDI)
   ========================================================================== */
function showLogModal() {
    document.getElementById('log-modal').style.display = 'flex';
    renderLog();
}

function hideLogModal() {
    document.getElementById('log-modal').style.display = 'none';
}

function renderLog() {
    const logList = document.getElementById('log-list');
    logList.innerHTML = '';
    if (!keypadLog.length) {
        logList.innerHTML = '<div style="color:#fff;">Nessun comando registrato</div>';
        return;
    }
    keypadLog.slice().reverse().forEach((entry, idx) => {
        logList.innerHTML += `<div style="display:flex; align-items:center; border-bottom:1px solid #333; padding:6px 0; color:#fff;">
            <span style="flex:1;">${entry.cmd} <span style='color:var(--accent-color); font-size:0.8em;'>[${entry.offset}]</span></span>
            <button onclick="playLogCmd(${keypadLog.length-1-idx})" style="background:var(--primary-color); color:#fff; border:none; border-radius:4px; padding:4px 8px; margin-left:8px; cursor:pointer;">▶</button>
        </div>`;
    });
}

function playLogCmd(idx) {
    let entry = keypadLog[idx];
    currentCmd = entry.cmd;
    sendStandalone(entry.offset, entry.step);
    hideLogModal();
}

function clearLog() {
    keypadLog = [];
    renderLog();
}
/* ==========================================================================
   3. SISTEMA STANDALONE (KEYPAD, COMANDI E MODALITÀ)
   ========================================================================== */
function k(v) {
    const d = document.getElementById('cmd-display');
    const off = document.getElementById('input-offset').value;
    const stp = parseInt(document.getElementById('input-spacing').value) || 1; // Recuperiamo lo step per il display locale



    // 1. GESTIONE RESET (C)
    if (v === 'C') {
        currentCmd = "";
        groupStart = 0;
        groupEnd = 0;
        groupStep = 1;
        d.innerText = isCheckMode ? "HL _" : "CHAN _";
        return;
    }

        // 2. GESTIONE INVIO (ENT)
    if (v === 'ENT' || v.includes('ENT')) {
        lastFaderVal = 0

        if (currentCmd.trim() !== "") {
            // 1. Salva la selezione pulita (senza AT) prima di aggiungere extra
            let atIdx = currentCmd.indexOf(" AT ");
            if (atIdx !== -1) {
                let strVal = currentCmd.substring(atIdx + 4).trim();
                lastFaderVal = (strVal === "FULL") ? 100 : parseInt(strVal) || 0;
            }
            let channelOnly = atIdx !== -1 ? currentCmd.substring(0, atIdx).trim() : currentCmd.trim();


            // 2. Componiamo il comando completo per il server (es. "1 AT 255")
            let extra = v.replace('ENT', '').trim();
            if (extra) currentCmd += " " + extra; 

            // Rileva gruppo THRU in Solo Mode
            if (isCheckMode && channelOnly.indexOf(" THRU ") !== -1) {
                let thruParts = channelOnly.split(" THRU ");
                groupStart = parseInt(thruParts[0].trim());
                groupEnd = parseInt(thruParts[1].trim());
                groupStep = parseInt(document.getElementById('input-spacing').value) || 1;
                channelOnly = String(groupStart);
                currentCmd = channelOnly; // ← aggiunto
                console.log("CMD prima di sendStandalone: nel ciclo", currentCmd);

            }           
console.log("CMD prima di sendStandalone: fuori dal ciclo ", currentCmd);

            // 3. Invio al C++
            sendStandalone();

            // 4. Gestione Display post-invio
            if (isCheckMode) {
                currentCmd = channelOnly; 
                d.innerText = "HL _ " + currentCmd;
            } else {
                currentCmd = channelOnly;
                if (channelOnly !== "") {
                    d.innerText = "CHAN _ " + currentCmd;
                } else {
                    d.innerText = "CHAN _";
                }
            }

        }
        return;
    }

    // 3. GESTIONE CLEAR (OUT)
    if (v === 'OUT') {
        fetch('/standalone?type=CLEAR');
        currentCmd = "";
        d.innerText = isCheckMode ? "HL _" : "CHAN _";
        return;
    }

    // 4. GESTIONE NAVIGAZIONE (NEXT / LAST)
if (v === 'NEXT' || v === 'LAST') {
    if (!isCheckMode || currentCmd.trim() === "") return;

    const off = document.getElementById('input-offset').value;
    const stp = parseInt(document.getElementById('input-spacing').value) || 1;

    // Se c'è un gruppo attivo naviga dentro il gruppo
    if (groupStart > 0 && groupEnd > 0) {
        let atIdx = currentCmd.indexOf(" AT ");
        let selectionPart = atIdx !== -1 ? currentCmd.substring(0, atIdx).trim() : currentCmd.trim();
        let pivot = parseInt(selectionPart) || groupStart;

        if (v === 'NEXT') {
            pivot += groupStep;
            if (pivot > groupEnd) pivot = groupStart; // wrap-around
        } else {
            pivot -= groupStep;
            if (pivot < groupStart) pivot = groupEnd; // wrap-around
        }

        currentCmd = String(pivot);
        fetch(`/standalone?cmd=${encodeURIComponent(currentCmd)}&type=${v}&offsets=${off}&step=${stp}&_t=${Date.now()}`);
        d.innerText = "HL _ " + currentCmd;
        return;
    }

    // Nessun gruppo — comportamento originale
    let atIdx = currentCmd.indexOf(" AT ");
    let selectionPart = atIdx !== -1 ? currentCmd.substring(0, atIdx).trim() : currentCmd.trim();
    let atPart = atIdx !== -1 ? currentCmd.substring(atIdx) : "";

    let numbersOnly = selectionPart.match(/\d+/g);
    if (!numbersOnly) return;
    let intNumbers = numbersOnly.map(Number);
    let minID = Math.min(...intNumbers);
    let maxID = Math.max(...intNumbers);
        let jump;
        if (selectionPart.indexOf(" THRU ") !== -1) {
            jump = (maxID - minID) + stp;
        } else {
            jump = stp;
        }    let offsetAmount = (v === 'NEXT' ? 1 : -1) * jump;

    let parts = selectionPart.trim().split(/(\+|\-|THRU|AT|FULL|OFF)/i);
    let newParts = parts.map(part => {
        let trimmed = part.trim();
        if (!trimmed) return "";
        if (!isNaN(trimmed)) {
            let val = parseInt(trimmed) + offsetAmount;
            return val < 1 ? 1 : (val > 512 ? 512 : val);
        }
        return trimmed;
    });

    currentCmd = newParts.filter(p => p !== "").join(" ") + atPart;
    fetch(`/standalone?cmd=${encodeURIComponent(currentCmd)}&type=${v}&offsets=${off}&step=${stp}&_t=${Date.now()}`);
    d.innerText = "HL _ " + currentCmd;
    return;
}
    // 5. GESTIONE MODALITÀ SOLO
    if (v === 'SOLO') {
        fetch(`/standalone?type=SOLO&offsets=${off}&step=${stp}`);
        
        // --- AGGIUNTA: Se usciamo dal SOLO (isCheckMode ora è false), puliamo tutto ---
        if (!isCheckMode) {
            currentCmd = "";
            d.innerText = "CHAN _";
        } else {
            // Se entriamo nel SOLO, mostriamo l'etichetta corretta
            d.innerText = "HL _ " + currentCmd;
        }
        return;
    }

    // 6. LOGICA DI SCRITTURA (Numeri e Operatori)

    let isNumber = !isNaN(v) || v === '.';
    if (isNumber) {
        currentCmd += v; 
    } else {
        // Se premo THRU o +, aggiungiamo gli spazi per leggibilità
        if (!currentCmd.endsWith(" ") && currentCmd !== "") {
            currentCmd += " " + v + " ";
        } else if (currentCmd === "" && (v === "THRU" || v === "+")) {
             // Evita di iniziare con un operatore se vuoto, o gestiscilo come preferisci
             currentCmd += v + " ";
        } else {
            currentCmd += v + " ";
        }
    }

    let cleaned = currentCmd.replace(/\s+/g, ' ');
    let label = isCheckMode ? "HL _ " : "CHAN _ ";
    d.innerText = label + cleaned;
}

// Funzione di invio e registrazione Log
let sendStandalone = function(offset, step) {
    if(typeof offset === 'undefined') offset = document.getElementById('input-offset').value;
    if(typeof step === 'undefined') step = document.getElementById('input-spacing').value;
    
    if(currentCmd.trim()) keypadLog.push({cmd: currentCmd.trim(), offset, step});
    
    fetch(`/standalone?cmd=${encodeURIComponent(currentCmd)}&offsets=${offset}&step=${step}`);
    if (!isCheckMode) {
            currentCmd = "";
            document.getElementById('cmd-display').innerText = "CHAN _";
        }
};


function toggleKeypadMode() {
    const toggle = document.getElementById('keypadToggle');
    const isChecked = toggle.checked;
    const display = document.getElementById('cmd-display');

    // 1. Feedback immediato in console
    console.log("Keypad Mode richiesto:", isChecked ? "ON" : "OFF");

    if (!isChecked) {
        currentCmd = ""; 
        // Reset solo mode
        if (isCheckMode) {
            isCheckMode = false;
            const btn = document.getElementById('btn-check-mode');
            if (btn) btn.classList.remove('active');
            fetch('/standalone?type=SOLO'); // manda SOLO OFF al C++
        }
         display.innerText = "OFF";
        display.style.color = "#ff4444"; // Rosso per pericolo/spento
        display.style.opacity = "0.5";   // Leggermente trasparente
        

    } else {
        // --- STATO ATTIVATO ---
         fetch('/release_snap');
        display.style.color = "";        // Torna al colore originale
        display.style.opacity = "1";
        display.innerText = isCheckMode ? "HL _" : "CHAN _";
    }

    // 2. Chiamata all'ESP32
    // Inviamo 1 se attivo, 0 se disattivo
    fetch(`/keypad_toggle?state=${isChecked ? 1 : 0}`)
        .then(response => response.text())
        .then(data => {
            if (data === "OK") {
                console.log("Stato Keypad aggiornato correttamente sull'ESP32");
            } else {
                console.error("Errore risposta ESP32:", data);
                // Se c'è un errore, riportiamo il toggle allo stato precedente
                toggle.checked = !isChecked;
            }
        })
        .catch(err => {
            console.error("Errore di connessione:", err);
            toggle.checked = !isChecked; // Revert in caso di crash rete
        })
        .finally(() => {
            // Aggiorniamo i badge della UI per riflettere il cambio (es. LIVE: STANDALONE)
            setTimeout(updateStatus, 500);
        });
}
/* ==========================================================================
   7. SISTEMA MACRO (SAVE & RUN)
   ========================================================================== */

/**
 * Apre il modal Macro. 
 * @param {string} mode - 'SAVE' (da REC macro) o 'RUN' (da Macro list)
 */
function openMacroModal(mode) {
    // 1. Check sicurezza: il salvataggio funziona solo se il Keypad è attivo
    const keypadToggle = document.getElementById('keypadToggle');
    if (mode === 'SAVE' && (!keypadToggle || !keypadToggle.checked)) {
        alert("Attiva il KEYPAD prima di registrare una Macro!");
        return;
    }

    currentMacroMode = mode;
    const modal = document.getElementById('macro-modal');
    const saveUI = document.getElementById('save-ui');
    const title = document.getElementById('macro-title');
    const btnRec = document.querySelector('.Macro-Rec');

    // 2. Configura l'aspetto in base alla modalità
    if (mode === 'SAVE') {
        title.innerText = "SALVA SCENA (KEYPAD)";
        saveUI.style.display = "block";
        modal.classList.add('modal-save-active'); // Per il CSS dashed
        
        if(btnRec) btnRec.classList.add('active');

        document.getElementById('macro-name').value = ""; 
        setTimeout(() => document.getElementById('macro-name').focus(), 100);
    } else {
        title.innerText = "ESECUZIONE MACRO";
        saveUI.style.display = "none";
        modal.classList.remove('modal-save-active');
        if(btnRec) btnRec.classList.remove('active');
    }

    // 3. Renderizza i bottoni e mostra
    renderMacroGrid();
    modal.style.setProperty('display', 'flex', 'important');
}

function closeMacroModal() {
    document.getElementById('macro-modal').style.display = 'none';
    // Se eravamo in REC, resettiamo visivamente il pulsante nel tastierino
    const recBtn = document.querySelector('.Macro-Rec');
    if (recBtn) recBtn.classList.remove('active');

    const btnRec = document.querySelector('.Macro-Rec');
    if(btnRec) btnRec.classList.remove('active');
}

/**
 * Genera dinamicamente i 10 bottoni della griglia
 */
function renderMacroGrid() {
    const container = document.getElementById('macro-grid');
    if (!container) return;
    container.innerHTML = "";

    for (let i = 0; i < 10; i++) {
        // Supponiamo che macroNames sia un array di 10 stringhe (es. da settings.macros)
        const name = (typeof settings !== 'undefined' && settings.macros) ? settings.macros[i] : "";
        const isOccupied = (name && name.trim() !== "");

        const btn = document.createElement('button');
        btn.className = `btn-macro-slot ${isOccupied ? 'occupied' : 'empty'}`;
        
        btn.innerHTML = `
            <span class="slot-num">M${i + 1}</span>
            <span class="slot-name">${isOccupied ? name : '--- VUOTO ---'}</span>
        `;

        btn.onclick = () => handleMacroAction(i);
        container.appendChild(btn);
    }
}

/**
 * Gestisce il click sullo slot (Salva o Esegue)
 */
function handleMacroAction(id) {
if (currentMacroMode === 'SAVE') {
        // --- LOGICA DI SALVATAGGIO CON CONFERMA ---
        
        // 1. Recuperiamo il nome attuale dallo stato (per sapere se è occupata)
        const existingName = (typeof settings !== 'undefined' && settings.macros) ? settings.macros[id] : "";
        const isOccupied = (existingName && existingName.trim() !== "" && existingName !== ",");

        // 2. Se lo slot è occupato, chiediamo conferma
        if (isOccupied) {
            const confirmMsg = `Lo slot M${id + 1} è già occupato da: "${existingName.trim()}".\n\nVuoi sovrascriverlo con i dati attuali?`;
            if (!confirm(confirmMsg)) {
                return; // L'utente ha annullato, usciamo dalla funzione
            }
        }

        // 3. Procediamo al recupero del nuovo nome dall'input
        const nameInput = document.getElementById('macro-name').value.trim();
        // Se l'input è vuoto, usiamo il nome vecchio (se c'era) o un default
        const finalName = nameInput || (isOccupied ? existingName.trim() : `Macro ${id + 1}`);
        
        // 4. Sanificazione veloce: evitiamo caratteri che rompono lo split del C++ (| e ,)
        const sanitizedName = finalName.replace(/[|,]/g, "").substring(0, 15);

        // 5. Chiamata al C++ per salvare
        fetch(`/save_macro?id=${id}&name=${encodeURIComponent(sanitizedName)}`)
            .then(r => r.text())
            .then(res => {
                if (res === "OK") {
                    console.log(`Macro ${id} salvata come: ${sanitizedName}`);
                    if (typeof settings !== 'undefined') settings.macros[id] = sanitizedName;
                    closeMacroModal();
                    updateStatus(); // Aggiorna i nomi per la prossima apertura
                } else {
                    alert("Errore salvataggio: " + res);
                }
            })
            .catch(err => console.error("Errore fetch macro:", err));

    } else {
        // Modalità RUN
        fetch(`/run_macro?id=${id}`)
            .then(r => r.text())
            .then(res => {
                if (res === "OK") {
                    console.log(`Esecuzione Macro ${id}`);
                    // Feedback opzionale sul display del keypad
                    document.getElementById('cmd-display').innerText = `LOAD M${id+1}...`;
                    setTimeout(() => {
                        document.getElementById('cmd-display').innerText = isCheckMode ? "HL _" : "CHAN _";
                    }, 1000);
                }
            });
    }
}
/* ==========================================================================
   SISTEMA SCENE — modal con tab GO / EDIT
   ========================================================================== */

let currentSnapTab = 'GO';
let snapCurrentPage = 0;
const SNAP_PER_PAGE = 10;
let isLiveForSnap = false; // aggiornato da updateStatus

function openSnapModal(startTab = 'GO') {
    // Determina se il sistema è live (per abilitare grab)
    isLiveForSnap = (typeof settings !== 'undefined' && settings._running) || false;
    
    const modal = document.getElementById('snap-modal');
    modal.style.setProperty('display', 'flex', 'important');
    setSnapTab(startTab);
}

function closeSnapModal() {
    document.getElementById('snap-modal').style.display = 'none';
}

function setSnapTab(tab) {
    currentSnapTab = tab;
    snapCurrentPage = 0;

    document.getElementById('snap-view-go').style.display   = (tab === 'GO')   ? 'block' : 'none';
    document.getElementById('snap-view-edit').style.display = (tab === 'EDIT') ? 'block' : 'none';

    document.getElementById('tab-go').classList.toggle('active',   tab === 'GO');
    document.getElementById('tab-edit').classList.toggle('active', tab === 'EDIT');

    if (tab === 'GO')   renderSnapGo();
    if (tab === 'EDIT') renderSnapEdit();
}

// ----- VISTA GO -----
function renderSnapGo() {
    const container = document.getElementById('snap-grid-go');
    if (!container) return;
    container.innerHTML = '';

    const snaps = (typeof settings !== 'undefined' && settings.snaps) ? settings.snaps : [];
    const start = snapCurrentPage * SNAP_PER_PAGE;
    const end   = Math.min(start + SNAP_PER_PAGE, snaps.length);

    for (let i = start; i < end; i++) {
        const name = snaps[i] || '';
        const isOccupied = name.trim() !== '';

        const btn = document.createElement('button');
        btn.className = `btn-snap-slot ${isOccupied ? 'occupied' : 'empty'}`;
        if (!isOccupied) btn.disabled = true;

        btn.innerHTML = `
            <span class="slot-num">SCENA ${i + 1}</span>
            <span class="slot-name">${isOccupied ? name : '--- LIBERO ---'}</span>
        `;

        if (isOccupied) {
            btn.onclick = () => {
                fetch(`/run_snap?id=${i}`)
                    .then(r => r.text())
                    .then(res => {
                        if (res === 'OK') {
                           
                            setTimeout(updateStatus, 300);
                        } else {
                            alert('Errore richiamo scena: ' + res);
                        }
                    });
            };
        }
        container.appendChild(btn);
    }

    // Aggiorna label pagina
    const totalPages = Math.ceil(50 / SNAP_PER_PAGE);
    document.getElementById('snap-page-label').innerText = `${snapCurrentPage + 1} / ${totalPages}`;
}

function snapPagePrev() {
    if (snapCurrentPage > 0) { snapCurrentPage--; renderSnapGo(); }
}

function snapPageNext() {
    const totalPages = Math.ceil(50 / SNAP_PER_PAGE);
    if (snapCurrentPage < totalPages - 1) { snapCurrentPage++; renderSnapGo(); }
}

// ----- VISTA EDIT -----
function renderSnapEdit() {
    const container = document.getElementById('snap-list-edit');
    if (!container) return;
    container.innerHTML = '';

    const snaps = (typeof settings !== 'undefined' && settings.snaps) ? settings.snaps : [];

    const grabUi = document.getElementById('snap-grab-ui');
    if (grabUi) grabUi.style.display = isLiveForSnap ? 'block' : 'none';

    for (let i = 0; i < 50; i++) {
        const name = snaps[i] || '';
        const isOccupied = name.trim() !== '';

        const row = document.createElement('div');
        row.className = 'snap-edit-row';
        row.id = `snap-row-${i}`;

        // Riga superiore: numero + nome
        const topRow = document.createElement('div');
        topRow.style.cssText = 'display:flex; align-items:center; gap:8px; width:100%;';

        const numSpan = document.createElement('span');
        numSpan.className = 'snap-edit-num';
        numSpan.innerText = `${i + 1}`;

        const nameSpan = document.createElement('span');
        nameSpan.className = `snap-edit-name ${isOccupied ? '' : 'empty'}`;
        nameSpan.innerText = isOccupied ? name : '— vuoto —';

        topRow.appendChild(numSpan);
        topRow.appendChild(nameSpan);

        // Riga inferiore: azioni — solo se occupato o grab disponibile
        const bottomRow = document.createElement('div');
        bottomRow.style.cssText = 'display:flex; gap:8px; margin-top:6px; width:100%;';

        // Input sposta (solo se occupato)
        if (isOccupied) {
            const moveWrap = document.createElement('div');
            moveWrap.style.cssText = 'display:flex; align-items:center; gap:4px; flex:1;';

            const moveLabel = document.createElement('span');
            moveLabel.style.cssText = 'font-size:0.7rem; color:#555; white-space:nowrap;';
            moveLabel.innerText = '→ slot';

            const moveInput = document.createElement('input');
            moveInput.type = 'text';
            moveInput.inputMode = 'numeric';
            moveInput.pattern = '[0-9]*';
            moveInput.className = 'snap-edit-move';
            moveInput.placeholder = String(i + 1);
            moveInput.min = 1;
            moveInput.max = 50;
            moveInput.style.cssText = 'width:56px; padding:6px; background:#222; border:1px solid #333; border-radius:4px; color:#aaa; font-size:0.8rem; text-align:center;';

            const moveBtn = document.createElement('button');
            moveBtn.style.cssText = 'padding:6px 10px; background:rgba(0,123,255,0.15); border:1px solid var(--primary); color:var(--primary); border-radius:4px; font-size:0.7rem; font-weight:bold; cursor:pointer; white-space:nowrap;';
            moveBtn.innerText = 'SPOSTA';
            moveBtn.onclick = () => {
                const dest = parseInt(moveInput.value) - 1;
                if (isNaN(dest) || dest < 0 || dest >= 50 || dest === i) {
                    moveInput.value = '';
                    return;
                }
                snapMove(i, dest);
                moveInput.value = '';
            };

            moveWrap.appendChild(moveLabel);
            moveWrap.appendChild(moveInput);
            moveWrap.appendChild(moveBtn);
            bottomRow.appendChild(moveWrap);

            // Pulsante elimina
            const delBtn = document.createElement('button');
            delBtn.style.cssText = 'padding:6px 12px; background:rgba(200,35,51,0.15); border:1px solid var(--danger); color:var(--danger); border-radius:4px; font-size:0.75rem; font-weight:bold; cursor:pointer; white-space:nowrap;';
            delBtn.innerText = '🗑 DEL';
            delBtn.onclick = () => snapDelete(i);
            bottomRow.appendChild(delBtn);

             // Pulsante rinomina
        const renameBtn = document.createElement('button');
        renameBtn.style.cssText = 'padding:6px 12px; background:rgba(0,212,255,0.1); border:1px solid var(--accent); color:var(--accent); border-radius:4px; font-size:0.75rem; font-weight:bold; cursor:pointer; white-space:nowrap;';
        renameBtn.innerText = '✏ EDIT';
        renameBtn.onclick = () => {
            // Toggle input rinomina inline
            const existing = row.querySelector('.rename-row');
            if (existing) { existing.remove(); return; }

            const renameRow = document.createElement('div');
            renameRow.className = 'rename-row';
            renameRow.style.cssText = 'display:flex; gap:8px; margin-top:8px; width:100%;';

            const renameInput = document.createElement('input');
            renameInput.type = 'text';
            renameInput.value = name;
            renameInput.maxLength = 15;
            renameInput.style.cssText = 'flex:1; padding:6px 10px; background:#111; border:1px solid var(--accent); border-radius:4px; color:#fff; font-size:0.85rem;';

            const confirmBtn = document.createElement('button');
            confirmBtn.style.cssText = 'padding:6px 12px; background:rgba(33,136,56,0.2); border:1px solid var(--success); color:var(--success); border-radius:4px; font-size:0.75rem; font-weight:bold; cursor:pointer;';
            confirmBtn.innerText = '✓ OK';
            confirmBtn.onclick = () => {
                const newName = renameInput.value.trim().replace(/[|,]/g, '').substring(0, 15);
                if (!newName) return;
                fetch(`/rename_snap?id=${i}&name=${encodeURIComponent(newName)}`)
                    .then(r => r.text())
                    .then(res => {
                        if (res === 'OK') {
                            if (typeof settings !== 'undefined') settings.snaps[i] = newName;
                            renderSnapEdit();
                            setTimeout(updateStatus, 300);
                        } else {
                            alert('Errore rinomina: ' + res);
                        }
                    });
            };

            renameRow.appendChild(renameInput);
            renameRow.appendChild(confirmBtn);
            row.appendChild(renameRow);
            setTimeout(() => renameInput.focus(), 50);
        };
        bottomRow.appendChild(renameBtn);

        }
       
        // Pulsante GRAB — sempre visibile, disabilitato in standby
        const grabBtn = document.createElement('button');
        grabBtn.style.cssText = 'padding:6px 12px; background:rgba(255,68,68,0.15); border:1px solid #ff4444; color:#ff4444; border-radius:4px; font-size:0.75rem; font-weight:bold; cursor:pointer; white-space:nowrap;';
        grabBtn.innerText = isOccupied ? '📸 OVR' : '📸 GRAB';
        grabBtn.disabled = !isLiveForSnap;
        grabBtn.title = isLiveForSnap ? 'Cattura output DMX' : 'Avvia una modalità per abilitare';
        if (!isLiveForSnap) grabBtn.style.opacity = '0.3';
        grabBtn.onclick = () => snapGrab(i);
        bottomRow.appendChild(grabBtn);

        row.appendChild(topRow);
        if (isOccupied || isLiveForSnap) row.appendChild(bottomRow);
        container.appendChild(row);
    }
}

// ----- AZIONI EDIT -----
function snapGrab(id) {
    const nameInput = document.getElementById('snap-name-input');
    const name = nameInput ? nameInput.value.trim() : '';
    const snaps = (typeof settings !== 'undefined' && settings.snaps) ? settings.snaps : [];
    const existingName = snaps[id] || '';
    const isOccupied = existingName.trim() !== '';

    if (isOccupied) {
        if (!confirm(`La Scena ${id + 1} contiene già: "${existingName.trim()}".\nSovrascrivere?`)) return;
    }

    const finalName = name || (isOccupied ? existingName.trim() : `Scena ${id + 1}`);
    const sanitized = finalName.replace(/[|,]/g, '').substring(0, 15);

    fetch(`/save_snap?id=${id}&name=${encodeURIComponent(sanitized)}`)
        .then(r => r.text())
        .then(res => {
            if (res === 'OK') {
                if (nameInput) nameInput.value = '';
                if (typeof settings !== 'undefined') settings.snaps[id] = sanitized;
                renderSnapEdit();
                setTimeout(updateStatus, 300);
            } else {
                alert('Errore grab: ' + res);
            }
        });
}

function snapDelete(id) {
    const snaps = (typeof settings !== 'undefined' && settings.snaps) ? settings.snaps : [];
    const name = snaps[id] || '';
    if (!confirm(`Eliminare la Scena ${id + 1} "${name}"?`)) return;

    fetch(`/delete_snap?id=${id}`)
        .then(r => r.text())
        .then(res => {
            if (res === 'OK') {
                if (typeof settings !== 'undefined') settings.snaps[id] = '';
                renderSnapEdit();
                setTimeout(updateStatus, 300);
            } else {
                alert('Errore eliminazione: ' + res);
            }
        });
}

function snapMove(fromId, toId) {
    fetch(`/move_snap?from=${fromId}&to=${toId}`)
        .then(r => r.text())
        .then(res => {
            if (res === 'OK') {
                fetch('/status')
                    .then(r => r.json())
                    .then(d => {
                        if (d.scenes) {
                            if (typeof settings === 'undefined') window.settings = {};
                            settings.snaps = d.scenes;
                        }
                        renderSnapEdit();
                    });
            } else {
                alert('Errore spostamento: ' + res);
            }
        });
}

// Aggiorna isLiveForSnap da updateStatus
// Aggiungere nella funzione updateStatus dopo aver estratto run:
// isLiveForSnap = run || sceneActive;
// e aggiornare settings._running = run;
function releaseScene() {
    fetch('/release_snap').then(() => updateStatus());
}

// dropdown menu off e spacing 

const state = {
  offset: { presets: [], current: '1' },
  spacing: { presets: [], current: '1' }
};

function renderList(type) {
  const list = document.getElementById('list-' + type);
  const s = state[type];
  if (!s.presets.length) {
    list.innerHTML = '<div class="empty-msg">Nessun preset salvato</div>';
    return;
  }
  list.innerHTML = s.presets.map((p, i) => `
    <div class="preset-item ${p === s.current ? 'selected' : ''}" onclick="selectPreset('${type}', '${p}')">
      <span class="preset-val">${p}</span>
      <button class="btn-del" onclick="event.stopPropagation(); deletePreset('${type}', ${i})">
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><polyline points="3 6 5 6 21 6"/>
      <path d="M19 6l-1 14H6L5 6"/><path d="M10 11v6M14 11v6"/><path d="M9 6V4h6v2"/>
      </svg>
      </button>
    </div>
  `).join('');
}

function selectPreset(type, val) {
  state[type].current = val;
  document.getElementById('input-' + type).value = val;
  closeAll();
  renderList(type);
}

function deletePreset(type, idx) {
    const val = state[type].presets[idx];
    
    // Elimina su ESP32
    fetch(`/delete_preset?type=${type}&value=${encodeURIComponent(val)}`)
        .then(r => r.json())
        .then(data => {
            state[type].presets = data;
            renderList(type);
        })
        .catch(() => {
            // Fallback locale
            state[type].presets.splice(idx, 1);
            renderList(type);
        });
}

function addPreset(type) {
    const inp = document.getElementById('add-' + type);
    const val = inp.value.trim();
    if (!val) return;
    
    // Salva su ESP32
    fetch(`/save_preset?type=${type}&value=${encodeURIComponent(val)}`)
        .then(r => r.json())
        .then(data => {
            state[type].presets = data;
            inp.value = '';
            selectPreset(type, val);
            renderList(type);
        })
        .catch(() => {
            // Fallback locale se ESP non risponde
            if (!state[type].presets.includes(val)) {
                state[type].presets.push(val);
            }
            inp.value = '';
            selectPreset(type, val);
            renderList(type);
        });
}

function toggleDropdown(type) {
  const dd = document.getElementById('dd-' + type);
  const btn = document.getElementById('btn-' + type);
  const isOpen = dd.classList.contains('open');
  closeAll();
  if (!isOpen) {
    dd.classList.add('open');
    btn.classList.add('open');
    renderList(type);
    setTimeout(() => document.getElementById('add-' + type).focus(), 50);
  }
}

function closeAll() {
  ['offset', 'spacing'].forEach(t => {
    document.getElementById('dd-' + t).classList.remove('open');
    document.getElementById('btn-' + t).classList.remove('open');
  });
}

document.addEventListener('click', e => {
  if (!e.target.closest('.field-wrap')) closeAll();
});

document.getElementById('add-offset').addEventListener('keydown', e => {
  if (e.key === 'Enter') addPreset('offset');
});
document.getElementById('add-spacing').addEventListener('keydown', e => {
  if (e.key === 'Enter') addPreset('spacing');
});

function loadPresetsFromESP() {
    fetch('/get_presets?type=offset')
        .then(r => r.json())
        .then(data => {
            state.offset.presets = data.length > 0 ? data : ['1'];
            renderList('offset');
        })
        .catch(() => {
            state.offset.presets = ['1'];
            renderList('offset');
        });

    fetch('/get_presets?type=spacing')
        .then(r => r.json())
        .then(data => {
            state.spacing.presets = data.length > 0 ? data : ['1'];
            renderList('spacing');
        })
        .catch(() => {
            state.spacing.presets = ['1'];
            renderList('spacing');
        });
}


renderList('offset');
renderList('spacing');
// blaskout 
function blackout() {
    fetch('/blackout').then(() => updateStatus());
}

function identify() {
    fetch('/identify')
}
function saveSetup() {
    const fadeSnap    = document.getElementById('setup-fade-snap').value;
    const fadeMacro   = document.getElementById('setup-fade-macro').value;
    const fadeKeypad  = document.getElementById('setup-fade-keypad').value;
    const soloLevelPct = document.getElementById('setup-solo-level').value;
    const blackoutAuto = "0"
    const autoSave    = document.getElementById('setup-autosave').checked ? "1" : "0";
    const fadeCurve = document.getElementById('setup-fade-curve').value;
    const ledmode = document.getElementById('setup-led').value;
    const easyPin = document.getElementById('setup-easy-pin').value.padStart(4, '0').substring(0, 4);
    

    // Aggiorna soloLevel globale immediatamente
    soloLevel = Math.round(parseInt(soloLevelPct) * 2.55);

    fetch(`/save_setup?fadesnap=${fadeSnap}&fademacro=${fadeMacro}&fadekeypad=${fadeKeypad}&sololevel=${soloLevelPct}&blackoutauto=${blackoutAuto}&autosave=${autoSave}&fadecurve=${fadeCurve}&ledmode=${ledmode}&easypin=${easyPin}`)
        .then(r => r.text())
        .then(res => {
            if (res === "OK") console.log("Setup salvato");
        })
        .catch(err => console.error("Errore saveSetup:", err));
}

function loadSetup() {
    fetch('/get_setup')
        .then(r => r.text())
        .then(data => {
            const p = data.split("|");
            if (p.length >= 6) {
                document.getElementById('setup-fade-snap').value    = p[0];
                document.getElementById('setup-fade-macro').value   = p[1];
                document.getElementById('setup-fade-keypad').value  = p[2];
                document.getElementById('setup-solo-level').value   = p[3];
                document.getElementById('setup-blackout-auto').value = p[4];
                document.getElementById('setup-autosave').checked   = p[5] === "1";
                document.getElementById('setup-fade-curve').value = p[6];
                document.getElementById('setup-led').value = p[7];
                document.getElementById('setup-easy-pin').value = p[8];
                
                // Aggiorna soloLevel globale
                soloLevel = Math.round(parseInt(p[3]) * 2.55);
            }
        })
        .catch(err => console.error("Errore loadSetup:", err));
}

function formatUptime(seconds) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return [h, m, s].map(v => String(v).padStart(2, '0')).join(':');
}

//
// FADER SETUP
//

// --- FADER ---
let faderCmd = "";
let faderOff = "1";
let faderStp = 1;

function openFader() {
    if (currentCmd.trim() === "") return; // nessuna selezione attiva
    
    faderCmd = currentCmd.trim();
    faderOff = document.getElementById('input-offset').value;
    faderStp = parseInt(document.getElementById('input-spacing').value) || 1;
    
    // Mostra la selezione nel modal
    document.getElementById('fader-selection').innerText = faderCmd;
    
    // Apri modal
    const modal = document.getElementById('fader-modal');
    modal.style.display = 'flex';
    
    // Inizializza fader a 0
    setFaderVal(lastFaderVal);
    
    // Aggiungi eventi touch/mouse
    initFaderEvents();
}

function closeFader() {
    lastFaderVal = parseInt(document.getElementById('fader-value').innerText) || 0;
    document.getElementById('fader-modal').style.display = 'none';
    removeFaderEvents();
}

let faderThrottle = null;

function setFaderVal(v) {
    v = Math.round(Math.max(0, Math.min(100, v)));
    document.getElementById('fader-value').innerText = v;
    
    const track = document.getElementById('fader-track');
    const fill = document.getElementById('fader-fill');
    const thumb = document.getElementById('fader-thumb');
    const trackH = track.clientHeight - 8;
    
    fill.style.height = (v / 100 * trackH) + 'px';
    thumb.style.bottom = (v / 100 * trackH - 12) + 'px';
    
    // Throttle fetch
    if (faderThrottle) return;
    faderThrottle = setTimeout(() => {
        fetch(`/fader?cmd=${encodeURIComponent(faderCmd)}&val=${v}&offsets=${faderOff}&step=${faderStp}`);
        faderThrottle = null;
    }, 30);
}

function getFaderValFromEvent(e) {
    const track = document.getElementById('fader-track');
    const rect = track.getBoundingClientRect();
    const clientY = e.touches ? e.touches[0].clientY : e.clientY;
    const relY = rect.bottom - clientY;
    return (relY / rect.height) * 100;
}

let faderDragging = false;

function initFaderEvents() {
    const track = document.getElementById('fader-track');
    track.addEventListener('mousedown', faderMouseDown);
    document.addEventListener('mousemove', faderMouseMove);
    document.addEventListener('mouseup', faderMouseUp);
    track.addEventListener('touchstart', faderTouchStart, { passive: false });
    track.addEventListener('touchmove', faderTouchMove, { passive: false });
}

function removeFaderEvents() {
    const track = document.getElementById('fader-track');
    track.removeEventListener('mousedown', faderMouseDown);
    document.removeEventListener('mousemove', faderMouseMove);
    document.removeEventListener('mouseup', faderMouseUp);
    track.removeEventListener('touchstart', faderTouchStart);
    track.removeEventListener('touchmove', faderTouchMove);
}

function faderMouseDown(e) { faderDragging = true; setFaderVal(getFaderValFromEvent(e)); }
function faderMouseMove(e) { if (faderDragging) setFaderVal(getFaderValFromEvent(e)); }
function faderMouseUp() { faderDragging = false; }
function faderTouchStart(e) { e.preventDefault(); setFaderVal(getFaderValFromEvent(e)); }
function faderTouchMove(e) { e.preventDefault(); setFaderVal(getFaderValFromEvent(e)); }