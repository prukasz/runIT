/**
 * runIT Web Studio - Client Application
 * Features:
 *  1. User Variable Directory & JSON Expansion Ladder
 *     - Inline field editing (NO POPUPS): Type select, Name input, Value input, Items count, Sub tickmark, Mut tickmark, Link select
 *     - Separate trees are distinctly separated in dedicated container cards
 *     - Top of tree mark [ROOT #id] & Link mark ↳ LINK: [#id]
 *     - Subfolders automatically choose PTR and create empty container ready to fill out
 *     - Clean technical indicators (all emojis removed)
 *  2. Dedicated Block Variables Inspector (Read-Only) with Next/Prev stepping
 *  3. JSON Ladder Viewer & Editor
 *  4. Wire Packet Compiler (Class 0x04)
 */

// ==========================================
// 1. STATE & DATA MODELS
// ==========================================

let nextNodeId = 1;

// SVG Icons for Subscribe (Eye) and Mutable (Pen / Lock) toggles
const ICONS = {
  eye: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path><circle cx="12" cy="12" r="3"></circle></svg>`,
  eyeOff: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"></path><line x1="1" y1="1" x2="23" y2="23"></line></svg>`,
  pen: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20h9"></path><path d="M16.5 3.5a2.121 2.121 0 0 1 3 3L7 19l-4 1 1-4L16.5 3.5z"></path></svg>`,
  lock: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"></rect><path d="M7 11V7a5 5 0 0 1 10 0v4"></path></svg>`
};

// Tree nodes: { id, name, type, count, initial, mutable, subscribe, parentId, linkToId, collapsed }
let variableNodes = [];

// Live Runtime Telemetry (Node-RED style) & Storage State
let isLiveMode = false;
let liveTelemetryInterval = null;
const STORAGE_KEY = "runit_variables_store";

// ==========================================
// 2. PRESETS INITIALIZATION
// ==========================================

// Helpers for 2D array dimension parsing & value counting
function parseCountOrDim(val) {
  if (!val) return { count: 1, dim: null };
  const str = String(val).trim().toLowerCase();
  const match = str.match(/^(\d+)\s*[xX*,\s]\s*(\d+)$/);
  if (match) {
    const rows = parseInt(match[1], 10) || 1;
    const cols = parseInt(match[2], 10) || 1;
    return { count: Math.max(1, rows * cols), dim: [rows, cols] };
  }
  const num = parseInt(str, 10);
  return { count: isNaN(num) ? 1 : Math.max(1, num), dim: null };
}

function countEnteredItems(rawStr) {
  if (!rawStr) return 0;
  const cleaned = rawStr.replace(/[\[\]]/g, " ");
  const tokens = cleaned.split(",").flatMap(s => s.trim().split(/\s+/)).map(s => s.trim()).filter(s => s.length > 0);
  return tokens.length;
}

const PRESETS = {
  master: [
    // === ROOT TREE 1: robot_cell (Deep Struct Hierarchy, Multiple Primitives & Subscriptions) ===
    { id: 1, name: "robot_cell", type: "PTR", count: 1, initial: "", mutable: true, subscribe: false, parentId: null, linkToId: null, collapsed: false },
    { id: 2, name: "joint_1", type: "PTR", count: 1, initial: "", mutable: true, subscribe: false, parentId: 1, linkToId: null, collapsed: false },
    { id: 3, name: "pos_actual", type: "F", count: 1, initial: "124.75", mutable: true, subscribe: true, parentId: 2, linkToId: null, collapsed: false },
    { id: 4, name: "pos_target", type: "F", count: 1, initial: "125.00", mutable: true, subscribe: false, parentId: 2, linkToId: null, collapsed: false },
    { id: 5, name: "encoder_pulses", type: "U32", count: 1, initial: "184290", mutable: true, subscribe: true, parentId: 2, linkToId: null, collapsed: false },
    { id: 6, name: "homed", type: "B", count: 1, initial: "true", mutable: true, subscribe: false, parentId: 2, linkToId: null, collapsed: false },
    { id: 7, name: "pid_gains", type: "PTR", count: 1, initial: "", mutable: true, subscribe: false, parentId: 2, linkToId: null, collapsed: false },
    { id: 8, name: "kp", type: "F", count: 1, initial: "2.45", mutable: true, subscribe: false, parentId: 7, linkToId: null, collapsed: false },
    { id: 9, name: "ki", type: "F", count: 1, initial: "0.05", mutable: true, subscribe: false, parentId: 7, linkToId: null, collapsed: false },
    { id: 10, name: "kd", type: "F", count: 1, initial: "0.12", mutable: true, subscribe: false, parentId: 7, linkToId: null, collapsed: false },
    { id: 11, name: "gripper", type: "PTR", count: 1, initial: "", mutable: true, subscribe: false, parentId: 1, linkToId: null, collapsed: false },
    { id: 12, name: "clamped", type: "B", count: 1, initial: "false", mutable: true, subscribe: true, parentId: 11, linkToId: null, collapsed: false },
    { id: 13, name: "grip_force_nm", type: "F", count: 1, initial: "14.2", mutable: true, subscribe: true, parentId: 11, linkToId: null, collapsed: false },
    { id: 14, name: "cell_state", type: "U8", count: 1, initial: "2", mutable: true, subscribe: true, parentId: 1, linkToId: null, collapsed: false },

    // === ROOT TREE 2: kinematics (2D Flat Matrix [3x4] + 1D Array + Partial Array) ===
    { id: 15, name: "kinematics", type: "PTR", count: 1, initial: "", mutable: true, subscribe: false, parentId: null, linkToId: null, collapsed: false },
    // 2D Contiguous Transformation Matrix (Float 3x4 = 12 elements)
    { id: 16, name: "transform_3x4", type: "F", count: 12, dim: [3, 4], initial: "[1.0, 0.0, 0.0, 250.0], [0.0, 1.0, 0.0, -120.5], [0.0, 0.0, 1.0, 85.2]", mutable: true, subscribe: false, parentId: 15, linkToId: null, collapsed: false },
    // 1D Vector (6 joint coordinates, all filled & subscribed)
    { id: 17, name: "joint_angles", type: "F", count: 6, initial: "0.0, 45.0, -30.0, 90.0, -45.0, 15.0", mutable: true, subscribe: true, parentId: 15, linkToId: null, collapsed: false },
    // 1D Array with partial slot fill (2 of 4 slots filled)
    { id: 18, name: "safety_limits", type: "I32", count: 4, initial: "-180, 180", mutable: true, subscribe: false, parentId: 15, linkToId: null, collapsed: false },

    // === ROOT TREE 3: trajectory_grid (2D Jagged / Relational Pointer Array of Row Vectors) ===
    { id: 19, name: "trajectory_grid", type: "PTR", count: 1, initial: "", mutable: true, subscribe: false, parentId: null, linkToId: null, collapsed: false },
    { id: 20, name: "waypoint_0", type: "F", count: 4, initial: "0.0, 0.0, 100.0, 0.0", mutable: true, subscribe: false, parentId: 19, linkToId: null, collapsed: false },
    { id: 21, name: "waypoint_1", type: "F", count: 4, initial: "50.0, 20.0, 120.0, 15.0", mutable: true, subscribe: false, parentId: 19, linkToId: null, collapsed: false },
    { id: 22, name: "waypoint_2", type: "F", count: 4, initial: "100.0, 80.0, 140.0, 45.0", mutable: true, subscribe: false, parentId: 19, linkToId: null, collapsed: false },

    // === ROOT TREE 4: active_tool_alias (Pointer Alias Linking to joint_1 with Redirected Subobjects) ===
    { id: 23, name: "active_tool_alias", type: "PTR", count: 1, initial: "", mutable: true, subscribe: false, parentId: null, linkToId: 2, collapsed: false },

    // === ROOT TREE 5: system_constants (Read-Only / Constant Values with Lock Icon) ===
    { id: 24, name: "system_constants", type: "PTR", count: 1, initial: "", mutable: false, subscribe: false, parentId: null, linkToId: null, collapsed: false },
    { id: 25, name: "firmware_version", type: "STR", count: 1, initial: "v2.4.0-esp32", mutable: false, subscribe: false, parentId: 24, linkToId: null, collapsed: false },
    { id: 26, name: "max_payload_kg", type: "F", count: 1, initial: "5.0", mutable: false, subscribe: false, parentId: 24, linkToId: null, collapsed: false },
    { id: 27, name: "hardware_serial", type: "U32", count: 1, initial: "9841203", mutable: false, subscribe: false, parentId: 24, linkToId: null, collapsed: false },

    // === ROOT STANDALONE VARIABLE (Top-Level Scalar alongside Trees) ===
    { id: 28, name: "emergency_stop", type: "B", count: 1, initial: "false", mutable: true, subscribe: true, parentId: null, linkToId: null, collapsed: false }
  ]
};

// ==========================================
// 3. TREE RENDERING ENGINE (INLINE FIELDS)
// ==========================================

function loadPreset(presetName) {
  if (!PRESETS[presetName]) return;
  variableNodes = JSON.parse(JSON.stringify(PRESETS[presetName]));
  const maxId = variableNodes.reduce((max, n) => Math.max(max, n.id), 0);
  nextNodeId = maxId + 1;
  renderTree();
}

function getNodeById(id) {
  return variableNodes.find(n => n.id === id);
}

function getChildren(parentId) {
  return variableNodes.filter(n => n.parentId === parentId);
}

function renderTree() {
  const container = document.getElementById("treeContainer");
  if (!container) return;

  const filterText = (document.getElementById("filterInput")?.value || "").toLowerCase().trim();
  container.innerHTML = "";

  const rootNodes = getChildren(null);

  if (rootNodes.length === 0) {
    container.innerHTML = `
      <div style="text-align: center; color: var(--text-dim); padding: 40px; font-family: var(--font-mono);">
        <div style="font-size: 1.1rem; margin-bottom: 8px;">[NO VARIABLE TREES DEFINED]</div>
        <div style="font-size: 0.85rem;">Click "+ New Root Tree (PTR)" or "+ New Root Variable" above to start.</div>
      </div>
    `;
    return;
  }

  // Render each root tree in its own distinct separated container card
  rootNodes.forEach(rootNode => {
    const card = renderRootTreeCard(rootNode, filterText);
    if (card) container.appendChild(card);
  });

  if (typeof saveToLocalStorage === "function") {
    saveToLocalStorage();
  }
  if (isLiveMode && typeof updateLivePillsInDOM === "function") {
    updateLivePillsInDOM();
  }
}

/**
 * Renders a distinct separated card container for a top-level root tree or root variable.
 */
function renderRootTreeCard(rootNode, filterText) {
  const children = getChildren(rootNode.id);

  // Filter check
  if (filterText) {
    const matchesSelf = (rootNode.name || "").toLowerCase().includes(filterText) ||
                        rootNode.type.toLowerCase().includes(filterText) ||
                        `#${rootNode.id}`.includes(filterText);
    const matchesAnyChild = hasMatchingDescendant(rootNode.id, filterText);
    if (!matchesSelf && !matchesAnyChild) return null;
  }

  const card = document.createElement("div");
  card.className = "tree-root-card";
  card.dataset.rootId = rootNode.id;

  // Render the root node's row
  const rootRowEl = renderNodeRow(rootNode, true);
  card.appendChild(rootRowEl);

  // If PTR and has children OR has linkToId, render children ladder
  if (rootNode.type === "PTR" && (children.length > 0 || rootNode.linkToId)) {
    const childrenContainer = document.createElement("div");
    childrenContainer.className = "tree-children" + (rootNode.collapsed ? " collapsed" : "");
    childrenContainer.id = `children-${rootNode.id}`;

    if (rootNode.linkToId) {
      // Role B: Pointer Alias / Link -> render redirected subobjects
      const linkedEls = renderRedirectedSubobjects(rootNode.linkToId, filterText);
      linkedEls.forEach(el => childrenContainer.appendChild(el));
    } else {
      // Role A: Concrete Container / Matrix -> render direct owned children
      children.forEach(child => {
        const childEl = renderSubtreeNode(child, filterText);
        if (childEl) childrenContainer.appendChild(childEl);
      });
    }

    card.appendChild(childrenContainer);
  }

  return card;
}

function hasMatchingDescendant(nodeId, filterText) {
  const children = getChildren(nodeId);
  for (const c of children) {
    if ((c.name || "").toLowerCase().includes(filterText) ||
        c.type.toLowerCase().includes(filterText) ||
        `#${c.id}`.includes(filterText)) {
      return true;
    }
    if (hasMatchingDescendant(c.id, filterText)) return true;
  }
  return false;
}

/**
 * Recursively renders a child node inside the ladder.
 */
function renderSubtreeNode(node, filterText) {
  const children = getChildren(node.id);

  if (filterText) {
    const matchesSelf = (node.name || "").toLowerCase().includes(filterText) ||
                        node.type.toLowerCase().includes(filterText) ||
                        `#${node.id}`.includes(filterText);
    const matchesAnyChild = hasMatchingDescendant(node.id, filterText);
    if (!matchesSelf && !matchesAnyChild) return null;
  }

  const wrapper = document.createElement("div");
  wrapper.className = "tree-node";
  wrapper.dataset.id = node.id;

  const rowEl = renderNodeRow(node, false);
  wrapper.appendChild(rowEl);

  if (node.type === "PTR" && (children.length > 0 || node.linkToId)) {
    const childrenContainer = document.createElement("div");
    childrenContainer.className = "tree-children" + (node.collapsed ? " collapsed" : "");
    childrenContainer.id = `children-${node.id}`;

    if (node.linkToId) {
      // Role B: Pointer Alias / Link -> render redirected subobjects
      const linkedEls = renderRedirectedSubobjects(node.linkToId, filterText);
      linkedEls.forEach(el => childrenContainer.appendChild(el));
    } else {
      // Role A: Concrete Container / Matrix -> render direct owned children
      children.forEach(child => {
        const childEl = renderSubtreeNode(child, filterText);
        if (childEl) childrenContainer.appendChild(childEl);
      });
    }

    wrapper.appendChild(childrenContainer);
  }

  return wrapper;
}

/**
 * Renders redirected subobjects for a container linking to targetId.
 */
function renderRedirectedSubobjects(targetId, filterText) {
  const targetNode = getNodeById(targetId);
  if (!targetNode) return [];

  const elements = [];
  const targetChildren = getChildren(targetNode.id);

  if (targetChildren.length > 0) {
    // Target is a struct/container with children: render each child as a redirected subobject
    targetChildren.forEach(child => {
      const el = createRedirectedRow(child, targetNode);
      if (el) elements.push(el);
    });
  } else {
    // Target is a scalar or array variable: render target itself as redirected subobject
    const el = createRedirectedRow(targetNode, targetNode);
    if (el) elements.push(el);
  }

  return elements;
}

function createRedirectedRow(item, sourceTarget) {
  const row = document.createElement("div");
  row.className = "tree-row-redirected";
  row.dataset.redirectFrom = item.id;

  // 1. Bullet
  const bullet = document.createElement("span");
  bullet.className = "tree-toggle";
  bullet.style.color = "var(--amber)";
  bullet.textContent = "↳";
  row.appendChild(bullet);

  // 2. Ref Badge
  const refBadge = document.createElement("span");
  refBadge.className = "tree-ref-badge";
  refBadge.textContent = `[REF #${item.id}]`;
  refBadge.title = `Reference to Object #${item.id}`;
  row.appendChild(refBadge);

  // 3. Type Badge
  const typeBadge = document.createElement("span");
  typeBadge.className = `tree-field-type type-${item.type.toLowerCase()}`;
  typeBadge.textContent = item.type + (item.count > 1 ? `[${item.count}]` : "");
  row.appendChild(typeBadge);

  // 4. Name
  const nameEl = document.createElement("span");
  nameEl.className = "tree-redirect-name";
  nameEl.textContent = item.name || `(anon_${item.id})`;
  row.appendChild(nameEl);

  // 5. Value
  if (item.type !== "PTR" && item.initial !== undefined && item.initial !== "") {
    const valEl = document.createElement("span");
    valEl.className = "tree-redirect-val";
    valEl.textContent = `= ${item.initial}`;
    row.appendChild(valEl);
  }

  // 6. Sub / Const tags
  if (item.subscribe) {
    const subTag = document.createElement("span");
    subTag.className = "tree-tag-sub";
    subTag.textContent = "LIVE";
    row.appendChild(subTag);
  }
  if (!item.mutable) {
    const constTag = document.createElement("span");
    constTag.className = "tree-tag-const";
    constTag.textContent = "CONST";
    row.appendChild(constTag);
  }

  // 7. Source description
  const sourceEl = document.createElement("span");
  sourceEl.className = "tree-redirect-source";
  const sourceName = sourceTarget.name || `Object #${sourceTarget.id}`;
  sourceEl.textContent = `(redirected from [#${sourceTarget.id} ${sourceName}])`;
  row.appendChild(sourceEl);

  // 8. Go to definition button
  const jumpBtn = document.createElement("button");
  jumpBtn.className = "tree-btn tree-btn-jump";
  jumpBtn.textContent = "[Go to def]";
  jumpBtn.title = `Highlight original Object #${item.id}`;
  jumpBtn.onclick = (e) => {
    e.stopPropagation();
    jumpToNode(item.id);
  };
  row.appendChild(jumpBtn);

  return row;
}

function jumpToNode(id) {
  const targetRow = document.querySelector(`.tree-node-box[data-id="${id}"]`) ||
                    document.querySelector(`.tree-row[data-id="${id}"]`) ||
                    document.querySelector(`[data-id="${id}"]`);
  if (targetRow) {
    targetRow.scrollIntoView({ behavior: "smooth", block: "center" });
    targetRow.style.transition = "all 0.3s ease";
    targetRow.style.boxShadow = "0 0 0 2px var(--accent), 0 0 14px var(--accent)";
    targetRow.style.borderColor = "var(--accent)";
    setTimeout(() => {
      targetRow.style.boxShadow = "";
      targetRow.style.borderColor = "";
    }, 1500);
  }
}

/**
 * Detects if a PTR container's children represent a uniform 2D matrix (PTR relational style)
 * e.g., M children all of the exact same non-PTR type with identical element counts.
 */
function detectPtrMatrix(children) {
  if (!children || children.length < 2) return null;
  const first = children[0];
  if (!first || first.type === "PTR" || first.count < 1) return null;
  const isUniform = children.every(c => c.type === first.type && c.count === first.count && c.type !== "PTR");
  if (isUniform) {
    return {
      rows: children.length,
      cols: first.count,
      cellType: first.type,
      totalElements: children.length * first.count
    };
  }
  return null;
}

/**
 * Renders a node container with two rows:
 *  - Main Row: Toggle, ID, Type (PTR / LINK), Name, Eye (Sub), Pen (Mut), Link (only when PTR), Actions
 *  - Sub Row: Count and Value with slots indicator
 */
function renderNodeRow(node, isRoot) {
  const box = document.createElement("div");
  box.className = "tree-node-box";
  box.dataset.id = node.id;

  const mainRow = document.createElement("div");
  mainRow.className = "tree-main-row";

  const subRow = document.createElement("div");
  subRow.className = "tree-sub-row";

  const children = getChildren(node.id);
  const isPtr = node.type === "PTR";
  const hasSubobjects = children.length > 0 || !!node.linkToId;
  const ptrMat = isPtr ? detectPtrMatrix(children) : null;

  // 1. Expand / Collapse toggle button
  const toggle = document.createElement("span");
  toggle.className = "tree-toggle";
  if (isPtr && hasSubobjects) {
    toggle.textContent = node.collapsed ? "[+]" : "[-]";
    toggle.title = "Toggle expand/collapse";
    toggle.onclick = (e) => {
      e.stopPropagation();
      node.collapsed = !node.collapsed;
      const childContainer = document.getElementById(`children-${node.id}`);
      if (childContainer) {
        childContainer.classList.toggle("collapsed", node.collapsed);
        toggle.textContent = node.collapsed ? "[+]" : "[-]";
      } else {
        renderTree();
      }
    };
  } else {
    toggle.innerHTML = "&middot;";
    toggle.style.opacity = "0.3";
  }
  mainRow.appendChild(toggle);

  // 2. Top-of-tree mark (if root) or ID badge
  if (isRoot) {
    const topMark = document.createElement("span");
    topMark.className = "tree-top-mark";
    topMark.textContent = isPtr ? `[ROOT TREE #${node.id}]` : `[ROOT VAR #${node.id}]`;
    mainRow.appendChild(topMark);
  } else {
    const idBadge = document.createElement("span");
    idBadge.className = "tree-id-badge";
    idBadge.textContent = `#${node.id}`;
    mainRow.appendChild(idBadge);
  }

  // 3. Field: Type (dropdown select) - PTR named as "PTR / LINK"
  const typeSelect = document.createElement("select");
  typeSelect.className = `tree-field-type type-${node.type.toLowerCase()}`;
  const typeOptions = [
    { val: "PTR", label: "PTR / LINK" },
    { val: "F", label: "F" },
    { val: "U32", label: "U32" },
    { val: "I32", label: "I32" },
    { val: "U8", label: "U8" },
    { val: "B", label: "B" },
    { val: "STR", label: "STR" }
  ];
  typeOptions.forEach(t => {
    const opt = document.createElement("option");
    opt.value = t.val;
    opt.textContent = t.label;
    if (t.val === node.type) opt.selected = true;
    typeSelect.appendChild(opt);
  });
  typeSelect.onchange = (e) => {
    const newType = e.target.value;
    node.type = newType;
    if (newType !== "PTR") {
      node.linkToId = null;
    }
    renderTree();
  };
  mainRow.appendChild(typeSelect);

  // 4. Field: Name (text input)
  const nameInput = document.createElement("input");
  nameInput.type = "text";
  nameInput.className = "tree-field-name";
  nameInput.value = node.name || "";
  nameInput.placeholder = isPtr ? "folder / struct name" : "variable name";
  nameInput.oninput = (e) => {
    node.name = e.target.value.trim();
  };
  mainRow.appendChild(nameInput);

  if (ptrMat) {
    const matBadge = document.createElement("span");
    matBadge.className = "tree-matrix-badge";
    matBadge.textContent = `[PTR MATRIX ${ptrMat.rows}×${ptrMat.cols}]`;
    matBadge.title = `PTR Relational Matrix: ${ptrMat.rows} rows × ${ptrMat.cols} cols of ${ptrMat.cellType} (${ptrMat.totalElements} elements total)`;
    mainRow.appendChild(matBadge);
  }

  // 5. Icon Toggle: Subscribe / Live Telemetry (Eye)
  const subBtn = document.createElement("button");
  subBtn.type = "button";
  subBtn.className = `tree-icon-toggle eye ${node.subscribe ? 'active' : 'inactive'}`;
  subBtn.title = node.subscribe ? "Live Telemetry: ACTIVE (click to mute)" : "Live Telemetry: MUTED (click to subscribe)";
  subBtn.innerHTML = node.subscribe ? ICONS.eye : ICONS.eyeOff;
  subBtn.onclick = (e) => {
    e.stopPropagation();
    node.subscribe = !node.subscribe;
    subBtn.className = `tree-icon-toggle eye ${node.subscribe ? 'active' : 'inactive'}`;
    subBtn.title = node.subscribe ? "Live Telemetry: ACTIVE (click to mute)" : "Live Telemetry: MUTED (click to subscribe)";
    subBtn.innerHTML = node.subscribe ? ICONS.eye : ICONS.eyeOff;
  };
  mainRow.appendChild(subBtn);

  // 6. Icon Toggle: Mutable / Changeable vs Constant (Pen / Lock)
  const isMut = node.mutable !== false;
  const mutBtn = document.createElement("button");
  mutBtn.type = "button";
  mutBtn.className = `tree-icon-toggle pen ${isMut ? 'active' : 'inactive'}`;
  mutBtn.title = isMut ? "Mutable / Writable: YES (click to lock as Constant)" : "Constant / Read-Only: LOCKED (click to make Mutable)";
  mutBtn.innerHTML = isMut ? ICONS.pen : ICONS.lock;
  mutBtn.onclick = (e) => {
    e.stopPropagation();
    node.mutable = !node.mutable;
    const nowMut = node.mutable !== false;
    mutBtn.className = `tree-icon-toggle pen ${nowMut ? 'active' : 'inactive'}`;
    mutBtn.title = nowMut ? "Mutable / Writable: YES (click to lock as Constant)" : "Constant / Read-Only: LOCKED (click to make Mutable)";
    mutBtn.innerHTML = nowMut ? ICONS.pen : ICONS.lock;
  };
  mainRow.appendChild(mutBtn);

  // 7. Field: Link to another Object ID (ONLY VISIBLE ON PTR NODES THAT DO NOT OWN CHILDREN)
  // A PTR node can be either a Concrete Struct/Matrix (owns children) OR a Pointer Alias (links to an object), not both!
  if (isPtr) {
    if (children.length === 0 || node.linkToId) {
      const linkSelect = document.createElement("select");
      linkSelect.className = "tree-field-link";
      linkSelect.title = "Link to another Object ID (inherits memory slice)";

      const noLinkOpt = document.createElement("option");
      noLinkOpt.value = "";
      noLinkOpt.textContent = "Link: (None)";
      linkSelect.appendChild(noLinkOpt);

      variableNodes.filter(n => n.id !== node.id).forEach(candidate => {
        const opt = document.createElement("option");
        opt.value = candidate.id;
        opt.textContent = `↳ Link: [#${candidate.id} ${candidate.name || 'anon'}]`;
        if (node.linkToId === candidate.id) opt.selected = true;
        linkSelect.appendChild(opt);
      });

      linkSelect.onchange = (e) => {
        const val = e.target.value;
        if (val) {
          node.linkToId = parseInt(val, 10);
          node.type = "PTR";
          node.collapsed = false;
        } else {
          node.linkToId = null;
        }
        renderTree();
      };
      mainRow.appendChild(linkSelect);

      if (node.linkToId) {
        const unlinkBtn = document.createElement("button");
        unlinkBtn.className = "tree-btn tree-btn-danger";
        unlinkBtn.style.padding = "2px 6px";
        unlinkBtn.style.fontSize = "0.7rem";
        unlinkBtn.textContent = "✕";
        unlinkBtn.title = "Remove link";
        unlinkBtn.onclick = (e) => {
          e.stopPropagation();
          node.linkToId = null;
          renderTree();
        };
        mainRow.appendChild(unlinkBtn);
      }
    } else {
      // If node owns children, it cannot be an alias link
      node.linkToId = null;
    }
  }

  // 8. Action Buttons: [+ Field], [+ Matrix (PTR)], [+ Subfolder], [Del]
  const actions = document.createElement("div");
  actions.className = "tree-actions";

  // Only show child addition buttons if this is NOT an alias link!
  if (isPtr && !node.linkToId) {
    const btnAddField = document.createElement("button");
    btnAddField.className = "tree-btn tree-btn-add";
    btnAddField.textContent = "+ Field";
    btnAddField.title = "Add new variable field into this container";
    btnAddField.onclick = (e) => {
      e.stopPropagation();
      addChildField(node.id);
    };
    actions.appendChild(btnAddField);

    const btnAddMatrix = document.createElement("button");
    btnAddMatrix.className = "tree-btn tree-btn-add";
    btnAddMatrix.style.color = "var(--cyan)";
    btnAddMatrix.style.borderColor = "rgba(57, 197, 207, 0.4)";
    btnAddMatrix.textContent = "+ Matrix (PTR)";
    btnAddMatrix.title = "Create a nested 2D matrix (PTR style) inside this container";
    btnAddMatrix.onclick = (e) => {
      e.stopPropagation();
      openMatrixModal(node.id);
    };
    actions.appendChild(btnAddMatrix);

    const btnAddSubfolder = document.createElement("button");
    btnAddSubfolder.className = "tree-btn tree-btn-add";
    btnAddSubfolder.textContent = "+ Subfolder";
    btnAddSubfolder.title = "Create empty subfolder (PTR) to fill out fields";
    btnAddSubfolder.onclick = (e) => {
      e.stopPropagation();
      addChildSubfolder(node.id);
    };
    actions.appendChild(btnAddSubfolder);
  }

  const btnDel = document.createElement("button");
  btnDel.className = "tree-btn tree-btn-danger";
  btnDel.textContent = isRoot ? "[Del Tree]" : "[Del]";
  btnDel.title = "Delete variable / struct";
  btnDel.onclick = (e) => {
    e.stopPropagation();
    deleteNode(node.id);
  };
  actions.appendChild(btnDel);

  mainRow.appendChild(actions);
  box.appendChild(mainRow);

  // --- SUB-ROW: Count & Value with slots indicator (ROW BELOW) ---
  const subLabel = document.createElement("span");
  subLabel.className = "tree-sub-label";
  subLabel.textContent = isPtr ? (ptrMat ? "matrix (ptr):" : "struct:") : "data:";
  subRow.appendChild(subLabel);

  // Count / Size / 2D Dim
  const countGroup = document.createElement("div");
  countGroup.className = "tree-count-group";
  countGroup.title = isPtr
    ? (ptrMat ? `PTR Matrix with ${ptrMat.rows} row arrays` : "Number of child fields in this container")
    : "Array element count or 2D dimension (e.g. 12 or 3x4)";

  const countLabel = document.createElement("span");
  countLabel.className = "tree-count-label";
  countLabel.textContent = isPtr ? (ptrMat ? "rows:" : "fields:") : (node.dim ? "dim:" : "count:");
  countGroup.appendChild(countLabel);

  const countInput = document.createElement("input");
  countInput.type = "text";
  countInput.className = "tree-field-count";
  if (isPtr) {
    countInput.value = ptrMat ? `${ptrMat.rows} rows` : Math.max(1, children.length);
    countInput.disabled = true;
    countInput.title = ptrMat ? `PTR Matrix with ${ptrMat.rows} row arrays` : `Container has ${children.length} fields`;
  } else {
    countInput.value = node.dim ? `${node.dim[0]}x${node.dim[1]}` : (node.count || 1);
    countInput.placeholder = "4 or 3x4";
    countInput.title = "Element count (e.g. 6) or 2D matrix dimension (e.g. 3x4)";
    countInput.oninput = (e) => {
      const parsed = parseCountOrDim(e.target.value);
      node.count = parsed.count;
      node.dim = parsed.dim;
      countLabel.textContent = node.dim ? "dim:" : "count:";
      updateValueSlotsBadge();
    };
  }
  countGroup.appendChild(countInput);
  subRow.appendChild(countGroup);

  // Value with Slots indicator
  const valGroup = document.createElement("div");
  valGroup.className = "tree-val-group";

  const valInput = document.createElement("input");
  valInput.type = "text";
  valInput.className = "tree-field-val";

  const slotsBadge = document.createElement("span");
  slotsBadge.className = "tree-val-slots-badge";

  function updateValueSlotsBadge() {
    if (isPtr) {
      if (ptrMat) {
        valInput.value = `(${ptrMat.rows} independent ${ptrMat.cellType}[${ptrMat.cols}] row arrays)`;
        valInput.disabled = true;
        valInput.style.width = "270px";
        slotsBadge.textContent = `${ptrMat.rows}×${ptrMat.cols} PTR Matrix (${ptrMat.totalElements} slots: ${ptrMat.rows} rows × ${ptrMat.cols} cols of ${ptrMat.cellType})`;
        slotsBadge.className = "tree-val-slots-badge slots-matrix-ptr";
        return;
      }
      valInput.value = node.linkToId ? "(linked reference)" : "(struct container)";
      valInput.disabled = true;
      if (node.linkToId) {
        const tNode = getNodeById(node.linkToId);
        const tChildren = tNode ? getChildren(tNode.id) : [];
        const cnt = tChildren.length > 0 ? tChildren.length : (tNode ? tNode.count : 0);
        slotsBadge.textContent = `${cnt} linked fields`;
        slotsBadge.className = "tree-val-slots-badge slots-struct";
      } else {
        slotsBadge.textContent = `${children.length} fields available`;
        slotsBadge.className = "tree-val-slots-badge slots-struct";
      }
      return;
    }

    valInput.disabled = false;
    const maxCount = node.count || 1;
    const rawVal = (valInput.value !== undefined) ? valInput.value : (node.initial || "");
    const filled = countEnteredItems(rawVal);

    // Dynamically expand input width when count is larger or text is longer
    const neededByCount = maxCount > 1 ? (maxCount * 56 + 30) : 160;
    const neededByText = (rawVal.length + 4) * 8.8;
    const computedWidth = Math.min(720, Math.max(160, Math.max(neededByCount, neededByText)));
    valInput.style.width = `${Math.round(computedWidth)}px`;

    // 2D Array Matrix representation
    if (node.dim) {
      const [rows, cols] = node.dim;
      valInput.placeholder = `e.g. [r0_c0..], [r1_c0..] (${rows}x${cols}=${maxCount} slots)`;
      if (filled === 0) {
        slotsBadge.textContent = `${rows}×${cols} matrix (${maxCount} slots: ${rows} rows × ${cols} cols)`;
        slotsBadge.className = "tree-val-slots-badge slots-partial";
      } else if (filled === maxCount) {
        slotsBadge.textContent = `${rows}×${cols} matrix (${filled}/${maxCount} filled: ${rows} rows × ${cols} cols)`;
        slotsBadge.className = "tree-val-slots-badge slots-complete";
      } else if (filled < maxCount) {
        const rowsFilled = Math.floor(filled / cols);
        const rem = filled % cols;
        const note = rem > 0 ? `${rowsFilled} rows + ${rem} cols` : `${rowsFilled} of ${rows} rows`;
        slotsBadge.textContent = `${rows}×${cols} matrix (${filled}/${maxCount} filled: ${note})`;
        slotsBadge.className = "tree-val-slots-badge slots-partial";
      } else {
        slotsBadge.textContent = `${rows}×${cols} matrix (${filled}/${maxCount} overflow)`;
        slotsBadge.className = "tree-val-slots-badge slots-overflow";
      }
      return;
    }

    // Standard 1D / Scalar
    if (maxCount === 1) {
      valInput.placeholder = "val (1 slot)";
      if (filled === 0) {
        slotsBadge.textContent = "1 slot available";
        slotsBadge.className = "tree-val-slots-badge";
      } else {
        slotsBadge.textContent = "1/1 filled";
        slotsBadge.className = "tree-val-slots-badge slots-complete";
      }
    } else {
      valInput.placeholder = `e.g. v1, v2... (${maxCount} slots available)`;
      if (filled === 0) {
        slotsBadge.textContent = `${maxCount} slots available`;
        slotsBadge.className = "tree-val-slots-badge slots-partial";
      } else if (filled === maxCount) {
        slotsBadge.textContent = `${filled}/${maxCount} filled`;
        slotsBadge.className = "tree-val-slots-badge slots-complete";
      } else if (filled < maxCount) {
        const remaining = maxCount - filled;
        slotsBadge.textContent = `${filled}/${maxCount} filled (${remaining} avail)`;
        slotsBadge.className = "tree-val-slots-badge slots-partial";
      } else {
        slotsBadge.textContent = `${filled}/${maxCount} overflow`;
        slotsBadge.className = "tree-val-slots-badge slots-overflow";
      }
    }
  }

  if (isPtr) {
    valInput.value = "(struct container)";
    valInput.disabled = true;
    valInput.style.width = "180px";
  } else {
    valInput.value = node.initial || "";
    valInput.oninput = (e) => {
      node.initial = e.target.value;
      updateValueSlotsBadge();
    };
  }

  updateValueSlotsBadge();
  valGroup.appendChild(valInput);
  valGroup.appendChild(slotsBadge);

  // Live Runtime Telemetry Pill (Node-RED style)
  const runtimePill = document.createElement("div");
  runtimePill.className = `tree-runtime-pill status-${node.runtimeStatus || 'idle'}`;
  runtimePill.id = `runtime-pill-${node.id}`;
  if (!isLiveMode) {
    runtimePill.style.display = "none";
  }
  const defaultPillText = node.runtimeValText || (node.subscribe ? 'standby' : 'idle');
  runtimePill.innerHTML = `<span class="dot"></span><span class="pill-text">${defaultPillText}</span>`;
  valGroup.appendChild(runtimePill);

  subRow.appendChild(valGroup);

  box.appendChild(subRow);
  return box;
}

// ==========================================
// 4. INLINE CREATION & DELETION LOGIC
// ==========================================

/**
 * Creates a new top-level root tree (PTR container)
 */
function addRootTree() {
  const newTree = {
    id: nextNodeId++,
    name: "",
    type: "PTR",
    count: 1,
    initial: "",
    parentId: null,
    linkToId: null,
    mutable: true,
    subscribe: false,
    collapsed: false
  };
  variableNodes.push(newTree);
  renderTree();

  setTimeout(() => {
    const card = document.querySelector(`.tree-root-card[data-root-id="${newTree.id}"] .tree-field-name`);
    if (card) card.focus();
  }, 50);
}

/**
 * Creates a new top-level scalar variable
 */
function addRootVar() {
  const newVar = {
    id: nextNodeId++,
    name: "",
    type: "F",
    count: 1,
    initial: "0.0",
    parentId: null,
    linkToId: null,
    mutable: true,
    subscribe: true,
    collapsed: false
  };
  variableNodes.push(newVar);
  renderTree();

  setTimeout(() => {
    const card = document.querySelector(`.tree-root-card[data-root-id="${newVar.id}"] .tree-field-name`);
    if (card) card.focus();
  }, 50);
}

/**
 * Adds a new child scalar field into an existing PTR parent
 */
function addChildField(parentId) {
  const parent = getNodeById(parentId);
  if (parent) parent.collapsed = false;

  const newField = {
    id: nextNodeId++,
    name: "",
    type: "F",
    count: 1,
    initial: "0.0",
    parentId: parentId,
    linkToId: null,
    mutable: true,
    subscribe: false,
    collapsed: false
  };
  variableNodes.push(newField);
  renderTree();

  setTimeout(() => {
    const row = document.querySelector(`[data-id="${newField.id}"] .tree-field-name`);
    if (row) row.focus();
  }, 50);
}

/**
 * Creates an empty subfolder (PTR is automatically chosen!) inside a parent container.
 * User can then fill out the fields directly inline.
 */
function addChildSubfolder(parentId) {
  const parent = getNodeById(parentId);
  if (parent) parent.collapsed = false;

  const newSubfolder = {
    id: nextNodeId++,
    name: "",
    type: "PTR", // PTR is automatically chosen when created subfolder!
    count: 1,
    initial: "",
    parentId: parentId,
    linkToId: null,
    mutable: true,
    subscribe: false,
    collapsed: false
  };
  variableNodes.push(newSubfolder);
  renderTree();

  setTimeout(() => {
    const row = document.querySelector(`[data-id="${newSubfolder.id}"] .tree-field-name`);
    if (row) row.focus();
  }, 50);
}

/**
 * Recursively deletes a node and all of its nested children.
 */
function deleteNode(id) {
  const idsToDelete = [id];
  let i = 0;
  while (i < idsToDelete.length) {
    const currId = idsToDelete[i++];
    const childIds = variableNodes.filter(n => n.parentId === currId).map(n => n.id);
    idsToDelete.push(...childIds);
  }

  variableNodes = variableNodes.filter(n => !idsToDelete.includes(n.id));
  variableNodes.forEach(n => {
    if (idsToDelete.includes(n.linkToId)) {
      n.linkToId = null;
    }
  });

  renderTree();
}

// ==========================================
// 5. FILE UPLOAD, EXPORT & OFFLINE PERSISTENCE (PWA STANDALONE)
// ==========================================

function flashToast(msg) {
  let toast = document.getElementById("studioToast");
  if (!toast) {
    toast = document.createElement("div");
    toast.id = "studioToast";
    toast.style.position = "fixed";
    toast.style.bottom = "24px";
    toast.style.right = "24px";
    toast.style.background = "var(--bg-secondary)";
    toast.style.border = "1px solid var(--emerald)";
    toast.style.color = "var(--emerald)";
    toast.style.padding = "10px 16px";
    toast.style.borderRadius = "var(--radius-md)";
    toast.style.fontFamily = "var(--font-mono)";
    toast.style.fontSize = "0.82rem";
    toast.style.boxShadow = "var(--shadow-lg)";
    toast.style.zIndex = "9999";
    toast.style.transition = "all 0.3s ease";
    document.body.appendChild(toast);
  }
  toast.textContent = `[STUDIO] ${msg}`;
  toast.style.opacity = "1";
  toast.style.transform = "translateY(0)";
  setTimeout(() => {
    toast.style.opacity = "0";
    toast.style.transform = "translateY(8px)";
  }, 2500);
}

function saveToLocalStorage() {
  try {
    const payload = {
      timestamp: Date.now(),
      nextNodeId: nextNodeId,
      objects: variableNodes.map(n => ({
        id: n.id,
        name: n.name || null,
        type: n.type,
        count: n.count,
        dim: n.dim || null,
        initial: n.initial || null,
        parentId: n.parentId,
        linkToId: n.linkToId,
        mutable: n.mutable,
        subscribe: n.subscribe,
        collapsed: n.collapsed || false
      }))
    };
    localStorage.setItem(STORAGE_KEY, JSON.stringify(payload));
  } catch (e) {
    // Storage quota or blocked
  }
}

function loadFromLocalStorage() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return false;
    const data = JSON.parse(raw);
    if (!data.objects || !Array.isArray(data.objects) || data.objects.length === 0) return false;

    variableNodes = data.objects.map(o => ({
      id: o.id,
      name: o.name || "",
      type: o.type || "F",
      count: o.count || 1,
      dim: Array.isArray(o.dim) && o.dim.length === 2 ? o.dim : null,
      initial: o.initial !== undefined && o.initial !== null ? String(o.initial) : "",
      parentId: o.parentId !== undefined ? o.parentId : (o.parent_id ?? null),
      linkToId: o.linkToId !== undefined ? o.linkToId : (o.link_to_id ?? null),
      mutable: o.mutable !== undefined ? o.mutable : true,
      subscribe: o.subscribe !== undefined ? o.subscribe : false,
      collapsed: !!o.collapsed
    }));

    const maxId = variableNodes.reduce((max, n) => Math.max(max, n.id), 0);
    nextNodeId = Math.max(maxId + 1, data.nextNodeId || 1);
    renderTree();
    return true;
  } catch (e) {
    return false;
  }
}

function uploadJsonFile(file) {
  if (!file) return;
  const reader = new FileReader();
  reader.onload = (e) => {
    try {
      const parsed = JSON.parse(e.target.result);
      let list = [];
      if (Array.isArray(parsed)) {
        list = parsed;
      } else if (parsed.objects && Array.isArray(parsed.objects)) {
        list = parsed.objects;
      } else {
        throw new Error("JSON file must contain an 'objects' array or be an array of variables.");
      }

      variableNodes = list.map(o => ({
        id: o.id || nextNodeId++,
        name: o.name || "",
        type: o.type || "F",
        count: o.count || 1,
        dim: Array.isArray(o.dim) && o.dim.length === 2 ? o.dim : null,
        initial: o.initial !== undefined && o.initial !== null ? String(o.initial) : "",
        parentId: (o.parentId !== undefined ? o.parentId : o.parent_id) ?? null,
        linkToId: (o.linkToId !== undefined ? o.linkToId : o.link_to_id) ?? null,
        mutable: o.mutable !== undefined ? o.mutable : true,
        subscribe: o.subscribe !== undefined ? o.subscribe : true,
        collapsed: false
      }));

      const maxId = variableNodes.reduce((max, n) => Math.max(max, n.id), 0);
      nextNodeId = maxId + 1;
      renderTree();
      flashToast(`Loaded ${variableNodes.length} variables from ${file.name}`);
    } catch (err) {
      alert(`Failed to import JSON: ${err.message}`);
    }
  };
  reader.readAsText(file);
}

function handleFileSelect(e) {
  const file = e.target.files && e.target.files[0];
  if (file) {
    uploadJsonFile(file);
  }
  e.target.value = "";
}

function downloadJsonFile() {
  const exportData = {
    program: "runit_user_variables",
    version: "1.0.0",
    exported_at: new Date().toISOString(),
    arena_size: 2048,
    objects: variableNodes.map(n => ({
      id: n.id,
      name: n.name || null,
      type: n.type,
      count: n.count,
      dim: n.dim || null,
      initial: n.initial || null,
      parent_id: n.parentId,
      link_to_id: n.linkToId,
      mutable: n.mutable,
      subscribe: n.subscribe
    }))
  };

  const str = JSON.stringify(exportData, null, 2);
  const blob = new Blob([str], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `runit_variables_${Date.now()}.json`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
  flashToast(`Exported ${variableNodes.length} variables to JSON`);
}

function initDragAndDrop() {
  const overlay = document.getElementById("dragDropOverlay");
  if (!overlay) return;

  let dragCounter = 0;

  window.addEventListener("dragenter", (e) => {
    e.preventDefault();
    dragCounter++;
    overlay.classList.add("active");
  });

  window.addEventListener("dragleave", (e) => {
    e.preventDefault();
    dragCounter--;
    if (dragCounter <= 0) {
      dragCounter = 0;
      overlay.classList.remove("active");
    }
  });

  window.addEventListener("dragover", (e) => {
    e.preventDefault();
  });

  window.addEventListener("drop", (e) => {
    e.preventDefault();
    dragCounter = 0;
    overlay.classList.remove("active");

    if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length > 0) {
      const file = e.dataTransfer.files[0];
      if (file.name.endsWith(".json") || file.type.includes("json")) {
        uploadJsonFile(file);
      } else {
        alert("Please drop a valid .json file.");
      }
    }
  });
}

// ==========================================
// 6. LIVE RUNTIME TELEMETRY ENGINE (NODE-RED STYLE)
// ==========================================

function toggleLiveMode() {
  isLiveMode = !isLiveMode;
  const btn = document.getElementById("btnToggleLive");
  if (btn) {
    if (isLiveMode) {
      btn.style.color = "var(--emerald)";
      btn.style.borderColor = "var(--emerald)";
      btn.style.background = "rgba(63, 185, 80, 0.15)";
      btn.innerHTML = `<span class="runtime-pulse-dot live"></span> Live Monitor: ACTIVE`;
      flashToast("Live Runtime Telemetry Monitor started");
    } else {
      btn.style.color = "var(--text-muted)";
      btn.style.borderColor = "var(--border-default)";
      btn.style.background = "";
      btn.innerHTML = `<span class="runtime-pulse-dot off"></span> Live Monitor: OFF`;
      flashToast("Live Runtime Telemetry stopped");
    }
  }

  if (isLiveMode) {
    simulateRuntimeTelemetryTick();
    liveTelemetryInterval = setInterval(simulateRuntimeTelemetryTick, 1200);
  } else {
    if (liveTelemetryInterval) {
      clearInterval(liveTelemetryInterval);
      liveTelemetryInterval = null;
    }
    updateLivePillsInDOM();
  }
}

function simulateRuntimeTelemetryTick() {
  const now = Date.now();

  variableNodes.forEach(node => {
    if (node.type === "PTR") {
      // Struct / Folder container: summarize state of children
      const children = getChildren(node.id);
      if (children.some(c => c.runtimeStatus === "err")) {
        node.runtimeStatus = "err";
        node.runtimeValText = "child err";
      } else if (children.some(c => c.runtimeStatus === "warn")) {
        node.runtimeStatus = "warn";
        node.runtimeValText = "child warn";
      } else if (children.some(c => c.runtimeStatus === "live")) {
        node.runtimeStatus = "live";
        node.runtimeValText = "active";
      } else {
        node.runtimeStatus = "idle";
        node.runtimeValText = "idle";
      }
      return;
    }

    if (!node.subscribe) {
      node.runtimeStatus = "idle";
      node.runtimeValText = node.mutable === false ? "const" : "muted";
      return;
    }

    // Live node with subscription active
    // Simulate realistic telemetry fluctuations & occasional Node-RED style status
    if (node.name === "cell_state") {
      node.runtimeStatus = "live";
      node.runtimeValText = "RUNNING (0x02)";
      return;
    }

    if (node.name === "emergency_stop") {
      node.runtimeStatus = "live";
      node.runtimeValText = "FALSE (OK)";
      return;
    }

    if (node.name === "grip_force_nm") {
      const base = 14.2;
      const val = (base + Math.sin(now / 1500) * 1.6).toFixed(2);
      if (parseFloat(val) > 15.3) {
        node.runtimeStatus = "warn";
        node.runtimeValText = `${val} N·m (HIGH)`;
        node.runtimeErr = "Grip force exceeds safety threshold (15.0 N·m)";
      } else {
        node.runtimeStatus = "live";
        node.runtimeValText = `${val} N·m`;
        node.runtimeErr = null;
      }
      return;
    }

    if (node.type === "F") {
      const base = parseFloat(node.initial) || 100.0;
      const delta = (Math.sin((now / 2000) + node.id) * 0.85);
      const val = (base + delta).toFixed(2);
      node.runtimeStatus = "live";
      node.runtimeValText = `${val}`;
    } else if (node.type === "U32" || node.type === "I32") {
      const base = parseInt(node.initial, 10) || 1000;
      const inc = Math.floor((now / 1000) % 500);
      node.runtimeStatus = "live";
      node.runtimeValText = `${base + inc}`;
    } else if (node.type === "B") {
      const isTrue = ((Math.floor(now / 3000) + node.id) % 2 === 0);
      node.runtimeStatus = "live";
      node.runtimeValText = isTrue ? "TRUE" : "FALSE";
    } else if (node.type === "U8") {
      const base = parseInt(node.initial, 10) || 0;
      node.runtimeStatus = "live";
      node.runtimeValText = `0x${((base + Math.floor(now / 1000)) & 0xFF).toString(16).toUpperCase()}`;
    } else {
      node.runtimeStatus = "live";
      node.runtimeValText = "online";
    }
  });

  // Periodically emit telemetry line into message stream monitor if running
  if (Math.random() < 0.35 && typeof StreamMonitor !== "undefined" && variableNodes.length > 0) {
    const subNodes = variableNodes.filter(n => n.subscribe && n.type !== "PTR");
    if (subNodes.length > 0) {
      const randNode = subNodes[Math.floor(Math.random() * subNodes.length)];
      StreamMonitor.addTelemetryLine(randNode, randNode.runtimeValText || randNode.initial);
    }
  }

  updateLivePillsInDOM();
}

function updateLivePillsInDOM() {
  variableNodes.forEach(node => {
    const pill = document.getElementById(`runtime-pill-${node.id}`);
    if (!pill) return;

    if (!isLiveMode) {
      pill.style.display = "none";
      return;
    }

    pill.style.display = "inline-flex";
    pill.className = `tree-runtime-pill status-${node.runtimeStatus || 'idle'}`;
    pill.innerHTML = `<span class="dot"></span><span class="pill-text">${node.runtimeValText || 'live'}</span>`;
    if (node.runtimeErr) {
      pill.title = `[ALERT] ${node.runtimeErr}`;
    } else {
      pill.title = `Live Runtime Telemetry: ${node.runtimeValText || ''}`;
    }
  });
}

// ==========================================
// 6.5. MESSAGE STREAM MONITOR & DECODER CONTROLLER
// ==========================================

const StreamMonitor = {
  messages: [],
  maxMessages: 500,
  currentFilter: "all",
  searchQuery: "",
  autoScroll: true,
  paused: false,
  isExpanded: false,
  isOpen: true,

  init() {
    this.bindEvents();
    // Pre-populate with typical ESP boot sequence
    this.addLogLine("I (120) [BOOT]: runIT ESP32-S3 firmware v2.4.0 started on Core-0");
    this.addLogLine("I (240) [VM]: Object arena initialized: 2048 bytes bump storage ready");
    this.addLogLine("I (480) [BLE]: Telemetry GATT service UUID 0x00FF initialized");
    this.addLogLine("I (650) [VM_EXEC]: Core-1 supervisor task running (pass 0)");
  },

  bindEvents() {
    // Top selector tabs
    document.querySelectorAll(".stream-tab").forEach(tab => {
      tab.addEventListener("click", () => {
        document.querySelectorAll(".stream-tab").forEach(t => t.classList.remove("active"));
        tab.classList.add("active");
        this.currentFilter = tab.dataset.stream;
        this.render();
      });
    });

    // Search filter
    document.getElementById("streamSearchInput")?.addEventListener("input", (e) => {
      this.searchQuery = e.target.value.toLowerCase().trim();
      this.render();
    });

    // Clear
    document.getElementById("btnClearStream")?.addEventListener("click", () => {
      this.messages = [];
      this.updateCounts();
      this.render();
    });

    // Auto-scroll toggle
    const btnAutoScroll = document.getElementById("btnAutoScroll");
    btnAutoScroll?.addEventListener("click", () => {
      this.autoScroll = !this.autoScroll;
      btnAutoScroll.classList.toggle("active", this.autoScroll);
      btnAutoScroll.textContent = `Auto-Scroll: ${this.autoScroll ? 'ON' : 'OFF'}`;
    });

    // Pause toggle
    const btnPause = document.getElementById("btnPauseStream");
    btnPause?.addEventListener("click", () => {
      this.paused = !this.paused;
      btnPause.classList.toggle("active", this.paused);
      btnPause.textContent = this.paused ? "Resume" : "Pause";
    });

    // Drawer resize toggle [↕]
    document.getElementById("btnResizeMonitor")?.addEventListener("click", () => {
      const drawer = document.getElementById("streamMonitorDrawer");
      this.isExpanded = !this.isExpanded;
      drawer.classList.toggle("expanded", this.isExpanded);
    });

    // Close / Hide Drawer
    document.getElementById("btnCloseMonitor")?.addEventListener("click", () => {
      this.toggleDrawer(false);
    });

    // Toggle Monitor button in top header
    document.getElementById("btnToggleMonitor")?.addEventListener("click", () => {
      this.toggleDrawer();
    });

    // Inject menu toggle & actions
    const btnInjectMenu = document.getElementById("btnInjectMenu");
    const injectDropdown = document.getElementById("injectMenuDropdown");
    btnInjectMenu?.addEventListener("click", (e) => {
      e.stopPropagation();
      injectDropdown?.classList.toggle("hidden");
    });

    document.addEventListener("click", () => {
      injectDropdown?.classList.add("hidden");
    });

    document.querySelectorAll(".inject-opt").forEach(opt => {
      opt.addEventListener("click", (e) => {
        e.stopPropagation();
        injectDropdown?.classList.add("hidden");
        const injectType = opt.dataset.inject;
        this.injectTestScenario(injectType);
      });
    });

    // Delegate click on interactive bi-lookup links
    document.getElementById("streamMessageList")?.addEventListener("click", (e) => {
      const refBtn = e.target.closest(".stream-ref-link");
      if (refBtn) {
        const objId = parseInt(refBtn.dataset.objId, 10);
        if (objId) {
          jumpToNode(objId);
        }
        return;
      }

      const hexBtn = e.target.closest(".stream-hex-toggle-btn");
      if (hexBtn) {
        const dump = hexBtn.nextElementSibling;
        if (dump) {
          dump.classList.toggle("hidden");
          hexBtn.textContent = dump.classList.contains("hidden") ? "[+ Hex]" : "[- Hex]";
        }
      }
    });
  },

  toggleDrawer(forceOpen = null) {
    const drawer = document.getElementById("streamMonitorDrawer");
    const btn = document.getElementById("btnToggleMonitor");
    if (!drawer) return;

    if (forceOpen === null) {
      this.isOpen = !this.isOpen;
    } else {
      this.isOpen = forceOpen;
    }

    if (this.isOpen) {
      drawer.classList.remove("hidden");
      btn?.classList.add("active");
    } else {
      drawer.classList.add("hidden");
      btn?.classList.remove("active");
    }
  },

  injectTestScenario(scenario) {
    if (scenario === "esp_logi") {
      this.addLogLine(`I (${Date.now() % 100000}) [VM]: Core-1 tick cycle pass OK, 0 faults`);
    } else if (scenario === "esp_logw") {
      this.addLogLine(`W (${Date.now() % 100000}) [PMU]: Voltage dropped to 3.19V on rail 3V3`);
    } else if (scenario === "err_oob") {
      // Accessor OOB referencing #3 pos_actual
      const rawPacket = StreamDecoder.createMockBinaryErrorPacket("accessor_oob", 3);
      this.processRawErrorPacket(rawPacket);
    } else if (scenario === "err_const") {
      // Accessor write protected on #24 system_constants
      const rawPacket = StreamDecoder.createMockBinaryErrorPacket("not_mutable", 24);
      this.processRawErrorPacket(rawPacket);
    } else if (scenario === "err_math") {
      // Divide by zero on Block #2
      const rawPacket = StreamDecoder.createMockBinaryErrorPacket("math_div_zero", null);
      this.processRawErrorPacket(rawPacket);
    } else if (scenario === "err_alloc") {
      // Arena memory exhausted
      const rawPacket = StreamDecoder.createMockBinaryErrorPacket("alloc_exhausted", null);
      this.processRawErrorPacket(rawPacket);
    }
  },

  addLogLine(rawText) {
    if (this.paused) return;
    const parsed = StreamDecoder.parseEspLogLine(rawText);
    const now = new Date();
    const timeStr = `${("0" + now.getHours()).slice(-2)}:${("0" + now.getMinutes()).slice(-2)}:${("0" + now.getSeconds()).slice(-2)}.${("00" + now.getMilliseconds()).slice(-3)}`;

    const msg = {
      id: Date.now() + Math.random(),
      timeStr: timeStr,
      type: StreamDecoder.STREAM_TYPE.ESP_LOG,
      level: parsed.level,
      tag: parsed.tag,
      owner: "ESP-IDF",
      text: parsed.message,
      relatedObjId: parsed.referencedObjId,
      relatedBlockIdx: parsed.referencedBlockIdx,
      rawHex: null
    };

    this.pushMessage(msg);
  },

  processRawErrorPacket(rawBytes) {
    if (this.paused) return;
    const decoded = StreamDecoder.decodeBinaryErrorPacket(rawBytes);
    const now = new Date();
    const timeStr = `${("0" + now.getHours()).slice(-2)}:${("0" + now.getMinutes()).slice(-2)}:${("0" + now.getSeconds()).slice(-2)}.${("00" + now.getMilliseconds()).slice(-3)}`;

    decoded.nodes.forEach(node => {
      const msg = {
        id: Date.now() + Math.random(),
        timeStr: timeStr,
        type: StreamDecoder.STREAM_TYPE.ERRORS,
        level: "ERROR",
        tag: node.tagName,
        owner: node.ownerDesc || node.ownerName,
        text: node.description,
        relatedObjId: node.relatedObjId,
        relatedBlockIdx: node.relatedBlockIdx,
        rawHex: decoded.rawHex
      };

      this.pushMessage(msg);

      // Bi-Lookup: Automatically reflect error on target variable node!
      if (node.relatedObjId) {
        const targetNode = getNodeById(node.relatedObjId);
        if (targetNode) {
          targetNode.runtimeStatus = "err";
          targetNode.runtimeErr = node.description;
          targetNode.runtimeValText = "FAULT";
          updateLivePillsInDOM();

          flashToast(`[FAULT ON #${targetNode.id} ${targetNode.name || 'anon'}]: ${node.description}`);
        }
      }
    });

    // Also push a RAW_HEX record for raw packet tab
    this.pushMessage({
      id: Date.now() + Math.random(),
      timeStr: timeStr,
      type: StreamDecoder.STREAM_TYPE.RAW_HEX,
      level: "HEX",
      tag: "ERR_WIRE_PKT (0x05)",
      owner: "Wire Rx",
      text: `Raw Error Packet (${rawBytes.length} bytes, ${decoded.nodeCount} node(s))`,
      relatedObjId: null,
      relatedBlockIdx: null,
      rawHex: decoded.rawHex
    });
  },

  addTelemetryLine(node, liveVal) {
    if (this.paused) return;
    const now = new Date();
    const timeStr = `${("0" + now.getHours()).slice(-2)}:${("0" + now.getMinutes()).slice(-2)}:${("0" + now.getSeconds()).slice(-2)}.${("00" + now.getMilliseconds()).slice(-3)}`;

    const msg = {
      id: Date.now() + Math.random(),
      timeStr: timeStr,
      type: StreamDecoder.STREAM_TYPE.TELEMETRY,
      level: "INFO",
      tag: "TELEMETRY",
      owner: "Subscribed Rx",
      text: `Object #${node.id} (${node.name || 'anon'}) stream value: ${liveVal}`,
      relatedObjId: node.id,
      relatedBlockIdx: null,
      rawHex: null
    };

    this.pushMessage(msg);
  },

  injectTxPacket(packet) {
    if (this.paused) return;
    const item = StreamDecoder.formatTxStreamItem(packet);
    const msg = {
      id: item.id,
      timeStr: item.timestamp,
      type: StreamDecoder.STREAM_TYPE.RAW_HEX,
      level: "TX",
      tag: item.tag,
      owner: "Host TX",
      text: `${item.title}: ${item.summary}`,
      relatedObjId: (item.decodedInfo && item.decodedInfo.fields && item.decodedInfo.fields.objectIds) ? item.decodedInfo.fields.objectIds[0] : null,
      relatedBlockIdx: (item.decodedInfo && item.decodedInfo.fields) ? item.decodedInfo.fields.blkIdx : null,
      rawHex: item.hexDump
    };
    this.pushMessage(msg);
  },

  pushMessage(msg) {
    this.messages.push(msg);
    if (this.messages.length > this.maxMessages) {
      this.messages.shift();
    }

    this.updateCounts();

    if (this.matchesFilter(msg)) {
      this.appendRowToDOM(msg);
    }
  },

  matchesFilter(msg) {
    if (this.currentFilter !== "all") {
      if (this.currentFilter === "esp_log" && msg.type !== StreamDecoder.STREAM_TYPE.ESP_LOG) return false;
      if (this.currentFilter === "errors" && msg.type !== StreamDecoder.STREAM_TYPE.ERRORS) return false;
      if (this.currentFilter === "telemetry" && msg.type !== StreamDecoder.STREAM_TYPE.TELEMETRY) return false;
      if (this.currentFilter === "raw_hex" && msg.type !== StreamDecoder.STREAM_TYPE.RAW_HEX && !msg.rawHex) return false;
    }

    if (this.searchQuery) {
      const full = `${msg.text} ${msg.tag} ${msg.owner} #${msg.relatedObjId}`.toLowerCase();
      if (!full.includes(this.searchQuery)) return false;
    }

    return true;
  },

  updateCounts() {
    const total = this.messages.length;
    const espCount = this.messages.filter(m => m.type === StreamDecoder.STREAM_TYPE.ESP_LOG).length;
    const errCount = this.messages.filter(m => m.type === StreamDecoder.STREAM_TYPE.ERRORS).length;
    const telCount = this.messages.filter(m => m.type === StreamDecoder.STREAM_TYPE.TELEMETRY).length;
    const hexCount = this.messages.filter(m => m.type === StreamDecoder.STREAM_TYPE.RAW_HEX || m.rawHex).length;

    const countEl = document.getElementById("streamMsgCount");
    if (countEl) countEl.textContent = `${total} msgs`;

    const bAll = document.getElementById("badgeAll");
    if (bAll) bAll.textContent = total;
    const bEsp = document.getElementById("badgeEspLog");
    if (bEsp) bEsp.textContent = espCount;
    const bErr = document.getElementById("badgeErrors");
    if (bErr) bErr.textContent = errCount;
    const bTel = document.getElementById("badgeTelemetry");
    if (bTel) bTel.textContent = telCount;
    const bHex = document.getElementById("badgeRawHex");
    if (bHex) bHex.textContent = hexCount;
  },

  render() {
    const list = document.getElementById("streamMessageList");
    if (!list) return;
    list.innerHTML = "";

    const filtered = this.messages.filter(m => this.matchesFilter(m));
    filtered.forEach(m => this.appendRowToDOM(m));
  },

  appendRowToDOM(msg) {
    const list = document.getElementById("streamMessageList");
    if (!list) return;

    const row = document.createElement("div");
    row.className = `stream-msg-row stream-type-${msg.type}`;

    let badgeClass = "tag-esp-info";
    if (msg.level === "TX") badgeClass = "tag-tx";
    else if (msg.type === StreamDecoder.STREAM_TYPE.ERRORS) badgeClass = "tag-vm-err";
    else if (msg.type === StreamDecoder.STREAM_TYPE.TELEMETRY) badgeClass = "tag-telemetry";
    else if (msg.type === StreamDecoder.STREAM_TYPE.RAW_HEX) badgeClass = "tag-raw-hex";
    else if (msg.level === "WARN") badgeClass = "tag-esp-warn";
    else if (msg.level === "ERROR") badgeClass = "tag-esp-error";

    let refLinkHtml = "";
    if (msg.relatedObjId) {
      const node = getNodeById(msg.relatedObjId);
      const nodeName = node ? (node.name || `anon_${node.id}`) : `Obj#${msg.relatedObjId}`;
      refLinkHtml = `<button class="stream-ref-link" data-obj-id="${msg.relatedObjId}" title="Bi-Lookup: Highlight Object #${msg.relatedObjId} in tree">↳ [#${msg.relatedObjId} ${nodeName}]</button>`;
    }

    let blockLinkHtml = "";
    if (msg.relatedBlockIdx !== null && msg.relatedBlockIdx !== undefined) {
      blockLinkHtml = `<span class="stream-block-link">[Block #${msg.relatedBlockIdx}]</span>`;
    }

    let hexHtml = "";
    if (msg.rawHex) {
      hexHtml = `
        <button class="stream-hex-toggle-btn">[+ Hex]</button>
        <div class="stream-hex-dump hidden">${msg.rawHex}</div>
      `;
    }

    row.innerHTML = `
      <span class="stream-msg-time">${msg.timeStr}</span>
      <span class="stream-tag-badge ${badgeClass}">${msg.tag}</span>
      <span class="stream-owner-tag">[${msg.owner}]</span>
      <div class="stream-msg-content">
        <span>${msg.text}</span>
        ${refLinkHtml}
        ${blockLinkHtml}
        ${hexHtml}
      </div>
    `;

    list.appendChild(row);

    if (this.autoScroll) {
      list.scrollTop = list.scrollHeight;
    }
  }
};

// ==========================================
// 7. JSON LADDER VIEW & EDIT
// ==========================================

function openJsonModal() {
  const editor = document.getElementById("jsonEditorArea");
  const jsonError = document.getElementById("jsonError");
  jsonError.textContent = "";

  const exportData = {
    program: "runit_user_variables",
    arena_size: 2048,
    objects: variableNodes.map(n => ({
      id: n.id,
      name: n.name || null,
      type: n.type,
      count: n.count,
      dim: n.dim || null,
      initial: n.initial || null,
      parent_id: n.parentId,
      link_to_id: n.linkToId,
      mutable: n.mutable,
      subscribe: n.subscribe
    }))
  };

  editor.value = JSON.stringify(exportData, null, 2);
  document.getElementById("jsonModal").classList.remove("hidden");
}

function applyJsonToTree() {
  const editor = document.getElementById("jsonEditorArea");
  const jsonError = document.getElementById("jsonError");
  try {
    const parsed = JSON.parse(editor.value);
    if (!parsed.objects || !Array.isArray(parsed.objects)) {
      throw new Error("Invalid format: Root object must contain 'objects' array");
    }

    variableNodes = parsed.objects.map(o => ({
      id: o.id || nextNodeId++,
      name: o.name || "",
      type: o.type || "F",
      count: o.count || 1,
      dim: Array.isArray(o.dim) && o.dim.length === 2 ? o.dim : null,
      initial: o.initial !== undefined && o.initial !== null ? String(o.initial) : "",
      parentId: o.parent_id !== undefined ? o.parent_id : (o.parentId ?? null),
      linkToId: o.link_to_id !== undefined ? o.link_to_id : (o.linkToId ?? null),
      mutable: o.mutable !== undefined ? o.mutable : true,
      subscribe: o.subscribe !== undefined ? o.subscribe : true,
      collapsed: false
    }));

    const maxId = variableNodes.reduce((max, n) => Math.max(max, n.id), 0);
    nextNodeId = maxId + 1;

    document.getElementById("jsonModal").classList.add("hidden");
    renderTree();
    flashToast(`Updated ${variableNodes.length} variables from JSON editor`);
  } catch (err) {
    jsonError.textContent = `JSON Error: ${err.message}`;
  }
}

// ==========================================
// 8. COMPILE WIRE PACKETS (CLASS 0x04) & TRANSMISSION CONTROLLER
// ==========================================

let lastCompiledData = null;
let activeBleDevice = null;
let activeBleCharacteristic = null;
let activeSerialPort = null;
let activeSerialWriter = null;

function compileClientSide(nodes) {
  // Pure client-side binary packet compiler for ESP32 VM Loader (Class 0x04)
  // Matches components/codecs/decoders/dec_vm_loader.h and Python VMProgram.compile()
  const CLASS_VM_LOADER = 0x04;
  const HEADER_PKT_VM_RESET = 0x40;
  const HEADER_PKT_VM_OPEN = 0x41;
  const HEADER_PKT_VM_ADD_OBJS = 0x42;
  const HEADER_PKT_VM_SET_DATA = 0x43;
  const HEADER_PKT_VM_SUBSCRIBE = 0x47;
  const HEADER_PKT_VM_EXEC = 0x48;

  const TYPE_CODES = {
    PTR: 1,
    U8: 2,
    U32: 3,
    I32: 4,
    F: 5,
    B: 6,
    STR: 7
  };

  // 1. Assign sequential IDs (0..N-1)
  const idMap = new Map();
  nodes.forEach((n, idx) => {
    idMap.set(n.id, idx);
  });

  // Calculate object sizing and total arena requirements
  let objBytes = 0;
  const objRecords = [];

  nodes.forEach((n, idx) => {
    const assignedId = idx;
    const typeCode = TYPE_CODES[n.type] || TYPE_CODES.F;
    const count = Math.max(1, n.count || 1);
    const elemSize = (n.type === "PTR") ? 2 : (n.type === "F" || n.type === "U32" || n.type === "I32") ? 4 : 1;
    const payloadSize = elemSize * count;
    const cleanName = (n.name || "").trim().slice(0, 15);
    const nameLen = cleanName.length;

    // Head byte 'd': obj_t (4 bits) | (name_size (4 bits) << 4)
    const d = (typeCode & 0x0F) | ((nameLen & 0x0F) << 4);

    // Head byte 'f': flags
    let f = 0x02; // UPD
    if (n.mutable !== false) f |= 0x01; // MUTABLE
    if (n.subscribe) f |= 0x04; // UPD_RESETABLE
    if (nameLen > 0) f |= 0x08; // TAGGED

    // 4-byte aligned footprint in arena
    const alignedSize = (4 + payloadSize + nameLen + 3) & ~3;
    objBytes += alignedSize;

    objRecords.push({
      id: assignedId,
      origId: n.id,
      node: n,
      payloadSize,
      d,
      f,
      name: cleanName,
      nameLen
    });
  });

  const objCount = nodes.length;
  const accCount = 0;
  const blkCount = 0;
  const registryBytes = (objCount + accCount + blkCount) * 4;
  const totalArenaBytes = Math.max(2048, (registryBytes + objBytes + 512 + 3) & ~3);

  const packets = [];
  let packetIdx = 1;

  function toHex(bytes) {
    return Array.from(bytes).map(b => ("00" + b.toString(16).toUpperCase()).slice(-2)).join(" ");
  }

  // 1. VM Reset (0x40)
  const resetBytes = new Uint8Array([CLASS_VM_LOADER, HEADER_PKT_VM_RESET]);
  packets.push({
    idx: packetIdx++,
    opcode: HEADER_PKT_VM_RESET,
    name: "1. VM Reset (0x40)",
    size: resetBytes.length,
    hex: toHex(resetBytes),
    bytes: Array.from(resetBytes),
    raw: resetBytes
  });

  // 2. VM Open Container (0x41)
  const openBuf = new ArrayBuffer(12);
  const openView = new DataView(openBuf);
  openView.setUint8(0, CLASS_VM_LOADER);
  openView.setUint8(1, HEADER_PKT_VM_OPEN);
  openView.setUint16(2, objCount, true);
  openView.setUint16(4, accCount, true);
  openView.setUint16(6, blkCount, true);
  openView.setUint32(8, totalArenaBytes, true);
  const openBytes = new Uint8Array(openBuf);
  packets.push({
    idx: packetIdx++,
    opcode: HEADER_PKT_VM_OPEN,
    name: "2. VM Open Container (0x41)",
    size: openBytes.length,
    hex: toHex(openBytes),
    bytes: Array.from(openBytes),
    raw: openBytes
  });

  // 3. Add Objects Batch (0x42)
  let currentChunk = [];
  let currentLen = 3;

  function flushObjChunk() {
    if (!currentChunk.length) return;
    const pktBuf = new Uint8Array(currentLen);
    pktBuf[0] = CLASS_VM_LOADER;
    pktBuf[1] = HEADER_PKT_VM_ADD_OBJS;
    pktBuf[2] = currentChunk.length;
    let off = 3;

    currentChunk.forEach(rec => {
      pktBuf[off++] = rec.id & 0xFF;
      pktBuf[off++] = (rec.id >> 8) & 0xFF;
      pktBuf[off++] = rec.payloadSize & 0xFF;
      pktBuf[off++] = (rec.payloadSize >> 8) & 0xFF;
      pktBuf[off++] = rec.d;
      pktBuf[off++] = rec.f;
      for (let i = 0; i < rec.nameLen; i++) {
        pktBuf[off++] = rec.name.charCodeAt(i);
      }
    });

    packets.push({
      idx: packetIdx++,
      opcode: HEADER_PKT_VM_ADD_OBJS,
      name: `3. Add Objects Batch (0x42)`,
      size: pktBuf.length,
      hex: toHex(pktBuf),
      bytes: Array.from(pktBuf),
      raw: pktBuf
    });

    currentChunk = [];
    currentLen = 3;
  }

  objRecords.forEach(rec => {
    const recLen = 6 + rec.nameLen;
    if (currentLen + recLen > 240 || currentChunk.length >= 255) {
      flushObjChunk();
    }
    currentChunk.push(rec);
    currentLen += recLen;
  });
  flushObjChunk();

  // 4. Seed Initial Data (0x43)
  const seedRecords = [];
  objRecords.forEach(rec => {
    const n = rec.node;
    let dataBytes = null;

    if (n.type === "PTR") {
      const children = nodes.filter(c => c.parentId === n.id);
      if (n.linkToId !== null && n.linkToId !== undefined) {
        const target = nodes.find(c => c.id === n.linkToId);
        if (target && !children.includes(target)) children.push(target);
      }
      if (children.length > 0) {
        const ptrBuf = new Uint8Array(children.length * 2);
        children.forEach((c, ci) => {
          const mappedCid = idMap.get(c.id) || 0;
          ptrBuf[ci * 2] = mappedCid & 0xFF;
          ptrBuf[ci * 2 + 1] = (mappedCid >> 8) & 0xFF;
        });
        dataBytes = ptrBuf;
      }
    } else if (n.initial !== undefined && n.initial !== null && String(n.initial).trim() !== "") {
      const rawStr = String(n.initial).replace(/\[|\]/g, " ");
      const parts = rawStr.split(/[\s,]+/).filter(x => x.length > 0);
      const count = Math.max(1, n.count || 1);

      if (n.type === "F") {
        const buf = new ArrayBuffer(count * 4);
        const view = new DataView(buf);
        for (let i = 0; i < count; i++) {
          const val = i < parts.length ? parseFloat(parts[i]) || 0 : 0;
          view.setFloat32(i * 4, val, true);
        }
        dataBytes = new Uint8Array(buf);
      } else if (n.type === "U32") {
        const buf = new ArrayBuffer(count * 4);
        const view = new DataView(buf);
        for (let i = 0; i < count; i++) {
          const val = i < parts.length ? parseInt(parts[i], 10) || 0 : 0;
          view.setUint32(i * 4, val, true);
        }
        dataBytes = new Uint8Array(buf);
      } else if (n.type === "I32") {
        const buf = new ArrayBuffer(count * 4);
        const view = new DataView(buf);
        for (let i = 0; i < count; i++) {
          const val = i < parts.length ? parseInt(parts[i], 10) || 0 : 0;
          view.setInt32(i * 4, val, true);
        }
        dataBytes = new Uint8Array(buf);
      } else if (n.type === "U8" || n.type === "B") {
        const buf = new Uint8Array(count);
        for (let i = 0; i < count; i++) {
          if (n.type === "B") {
            const v = i < parts.length ? parts[i].toLowerCase() : "0";
            buf[i] = (v === "true" || v === "1" || v === "t") ? 1 : 0;
          } else {
            buf[i] = (i < parts.length ? parseInt(parts[i], 10) || 0 : 0) & 0xFF;
          }
        }
        dataBytes = buf;
      }
    }

    if (dataBytes && dataBytes.length > 0) {
      seedRecords.push({ id: rec.id, startIdx: 0, data: dataBytes });
    }
  });

  if (seedRecords.length > 0) {
    let currentSeedChunk = [];
    let currentSeedLen = 3;

    function flushSeedChunk() {
      if (!currentSeedChunk.length) return;
      const pktBuf = new Uint8Array(currentSeedLen);
      pktBuf[0] = CLASS_VM_LOADER;
      pktBuf[1] = HEADER_PKT_VM_SET_DATA;
      pktBuf[2] = currentSeedChunk.length;
      let off = 3;

      currentSeedChunk.forEach(sr => {
        pktBuf[off++] = sr.id & 0xFF;
        pktBuf[off++] = (sr.id >> 8) & 0xFF;
        pktBuf[off++] = sr.startIdx & 0xFF;
        pktBuf[off++] = (sr.startIdx >> 8) & 0xFF;
        pktBuf[off++] = sr.data.length & 0xFF;
        pktBuf[off++] = (sr.data.length >> 8) & 0xFF;
        pktBuf.set(sr.data, off);
        off += sr.data.length;
      });

      packets.push({
        idx: packetIdx++,
        opcode: HEADER_PKT_VM_SET_DATA,
        name: `4. Seed Initial Data (0x43)`,
        size: pktBuf.length,
        hex: toHex(pktBuf),
        bytes: Array.from(pktBuf),
        raw: pktBuf
      });

      currentSeedChunk = [];
      currentSeedLen = 3;
    }

    seedRecords.forEach(sr => {
      const recLen = 6 + sr.data.length;
      if (currentSeedLen + recLen > 240 || currentSeedChunk.length >= 255) {
        flushSeedChunk();
      }
      currentSeedChunk.push(sr);
      currentSeedLen += recLen;
    });
    flushSeedChunk();
  }

  // 5. Subscribe Live Telemetry (0x47)
  const subIds = [];
  objRecords.forEach(rec => {
    if (rec.node.subscribe) subIds.push(rec.id);
  });
  if (subIds.length > 0) {
    const subBuf = new Uint8Array(3 + subIds.length * 2);
    subBuf[0] = CLASS_VM_LOADER;
    subBuf[1] = HEADER_PKT_VM_SUBSCRIBE;
    subBuf[2] = subIds.length;
    subIds.forEach((sid, i) => {
      subBuf[3 + i * 2] = sid & 0xFF;
      subBuf[3 + i * 2 + 1] = (sid >> 8) & 0xFF;
    });
    packets.push({
      idx: packetIdx++,
      opcode: HEADER_PKT_VM_SUBSCRIBE,
      name: `5. Subscribe Live Telemetry (0x47)`,
      size: subBuf.length,
      hex: toHex(subBuf),
      bytes: Array.from(subBuf),
      raw: subBuf
    });
  }

  // 6. Execution Start (0x48 Normal Mode)
  const execBuf = new Uint8Array([CLASS_VM_LOADER, HEADER_PKT_VM_EXEC, 0x05]);
  packets.push({
    idx: packetIdx++,
    opcode: HEADER_PKT_VM_EXEC,
    name: `6. Execution Start (0x48 NORMAL_MODE)`,
    size: execBuf.length,
    hex: toHex(execBuf),
    bytes: Array.from(execBuf),
    raw: execBuf
  });

  return {
    success: true,
    packet_count: packets.length,
    total_arena_size: totalArenaBytes,
    packets: packets
  };
}

async function compileProgram() {
  const modal = document.getElementById("compileModal");
  const meta = document.getElementById("compileMeta");
  const list = document.getElementById("packetList");
  const progressBox = document.getElementById("txProgressContainer");

  if (progressBox) progressBox.classList.add("hidden");
  meta.innerHTML = `<span>[COMPILING CLASS 0x04 WIRE PACKETS...]</span>`;
  list.innerHTML = "";
  modal.classList.remove("hidden");

  const payload = {
    name: "runit_web_prog",
    arena_size: 2048,
    objects: variableNodes.map(n => ({
      id: n.id,
      name: n.name || null,
      type: n.type,
      count: n.count,
      dim: n.dim || null,
      initial: n.initial || null,
      parent_id: n.parentId,
      link_to_id: n.linkToId,
      mutable: n.mutable,
      subscribe: n.subscribe
    }))
  };

  let compileData = null;
  let isStandaloneFallback = false;

  try {
    const res = await fetch("/api/compile", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });

    if (res.ok) {
      compileData = await res.json();
    } else {
      throw new Error(`HTTP ${res.status}`);
    }
  } catch (err) {
    // Standalone PWA fallback: pure client-side compilation!
    compileData = compileClientSide(variableNodes);
    isStandaloneFallback = true;
  }

  if (!compileData || !compileData.success) {
    meta.innerHTML = `<span style="color: var(--rose);">[COMPILATION ERROR: ${compileData ? compileData.error : 'Unknown'}]</span>`;
    return;
  }

  // Ensure each packet has raw Uint8Array and bytes list
  compileData.packets.forEach(p => {
    if (!p.bytes && p.hex) {
      p.bytes = p.hex.split(" ").map(h => parseInt(h, 16));
    }
    if (!p.raw && p.bytes) {
      p.raw = new Uint8Array(p.bytes);
    }
  });

  lastCompiledData = compileData;

  meta.innerHTML = `
    <div style="display: flex; gap: 20px; align-items: center; flex-wrap: wrap;">
      <span style="color: var(--emerald); font-weight: 600;">
        ${isStandaloneFallback ? '[STANDALONE / CLIENT-SIDE COMPILED] (Offline PWA)' : '[COMPILED VIA RUNTIME BACKEND]'}
      </span>
      <span>Packets: <strong>${compileData.packet_count}</strong></span>
      <span>Estimated Arena: <strong>${compileData.total_arena_size} bytes</strong></span>
      <span style="color: var(--cyan);">Ready to be Sent (Class 0x04)</span>
    </div>
  `;

  list.innerHTML = compileData.packets.map(p => {
    const rawBytes = p.bytes ? new Uint8Array(p.bytes) : (p.raw || new Uint8Array(0));
    const decoded = StreamDecoder.decodeVmLoaderPacket(rawBytes);

    return `
      <div class="packet-card" data-pkt-idx="${p.idx}">
        <div class="packet-card-header">
          <div style="display: flex; align-items: center; gap: 8px;">
            <span class="packet-opcode-pill">${decoded.opcodeHex || '0x??'}</span>
            <span style="font-weight: 600; color: var(--text-main);">Packet #${p.idx}: ${p.name}</span>
            <span style="color: var(--text-dim); font-size: 0.75rem;">(${p.size} Bytes)</span>
          </div>
          <div class="packet-btn-group">
            <button class="btn btn-sm btn-pkt-send" data-idx="${p.idx}" style="background: rgba(16, 185, 129, 0.15); border-color: rgba(16, 185, 129, 0.4); color: var(--emerald);" title="Send this packet individually">
              ▶ Send
            </button>
            <button class="btn btn-sm btn-pkt-copy" data-idx="${p.idx}" title="Copy hex">
              📋 Copy
            </button>
          </div>
        </div>
        <div style="font-size: 0.78rem; color: var(--text-muted); margin-bottom: 6px;">
          ${decoded.desc || ''}
        </div>
        <div style="color: var(--text-main); word-break: break-all; letter-spacing: 1px; font-size: 0.74rem; background: rgba(0,0,0,0.3); padding: 5px 8px; border-radius: 3px; border: 1px solid rgba(255,255,255,0.04);">
          ${p.hex}
        </div>
      </div>
    `;
  }).join("");
}

// --------------------------------------------------------
// TRANSMISSION & EXPORT LOGIC (READY TO BE SENT)
// --------------------------------------------------------

async function sendAllPackets() {
  if (!lastCompiledData || !lastCompiledData.packets || !lastCompiledData.packets.length) {
    flashToast("No compiled packets to send. Compile program first.");
    return;
  }

  const transport = document.getElementById("selTxTransport")?.value || "stream";
  const pacingMs = parseInt(document.getElementById("selTxPacing")?.value || "40", 10);
  const progressBox = document.getElementById("txProgressContainer");
  const progressBar = document.getElementById("txProgressBar");
  const progressLabel = document.getElementById("txProgressLabel");
  const progressPct = document.getElementById("txProgressPct");

  if (progressBox) progressBox.classList.remove("hidden");

  // Web BLE connection handling
  if (transport === "web_ble" && !activeBleCharacteristic) {
    try {
      if (progressLabel) progressLabel.textContent = "Connecting to BLE device...";
      await connectWebBluetooth();
    } catch (e) {
      flashToast(`BLE Connection Failed: ${e.message}`);
      if (progressBox) progressBox.classList.add("hidden");
      return;
    }
  } else if (transport === "web_serial" && !activeSerialWriter) {
    try {
      if (progressLabel) progressLabel.textContent = "Opening USB Serial port...";
      await connectWebSerial();
    } catch (e) {
      flashToast(`Serial Connection Failed: ${e.message}`);
      if (progressBox) progressBox.classList.add("hidden");
      return;
    }
  }

  const total = lastCompiledData.packets.length;

  for (let i = 0; i < total; i++) {
    const pkt = lastCompiledData.packets[i];
    const pct = Math.round(((i + 1) / total) * 100);

    if (progressBar) progressBar.style.width = `${pct}%`;
    if (progressPct) progressPct.textContent = `${pct}%`;
    if (progressLabel) progressLabel.textContent = `[${i + 1}/${total}] Transmitting ${pkt.name}...`;

    try {
      await transmitSinglePacketRaw(pkt, transport);
    } catch (err) {
      if (progressLabel) progressLabel.textContent = `Failed at packet #${i + 1}: ${err.message}`;
      flashToast(`TX Error: ${err.message}`);
      return;
    }

    if (i < total - 1 && pacingMs > 0) {
      await new Promise(r => setTimeout(r, pacingMs));
    }
  }

  if (progressLabel) {
    progressLabel.innerHTML = `✅ All ${total} packets transmitted successfully (${pacingMs}ms pacing)`;
  }
  flashToast(`Transmitted ${total} packets to ${transport === 'stream' ? 'Stream Monitor' : transport}`);
}

async function sendSinglePacket(pktIdx) {
  if (!lastCompiledData || !lastCompiledData.packets) return;
  const pkt = lastCompiledData.packets.find(p => p.idx === pktIdx);
  if (!pkt) return;

  const transport = document.getElementById("selTxTransport")?.value || "stream";

  try {
    await transmitSinglePacketRaw(pkt, transport);
    flashToast(`Sent Packet #${pkt.idx}: ${pkt.name}`);
  } catch (err) {
    flashToast(`TX Error: ${err.message}`);
  }
}

async function transmitSinglePacketRaw(pkt, transport) {
  const rawBytes = pkt.bytes ? new Uint8Array(pkt.bytes) : (pkt.raw || new Uint8Array(0));

  if (transport === "web_ble" && activeBleCharacteristic) {
    await activeBleCharacteristic.writeValueWithoutResponse(rawBytes);
  } else if (transport === "web_serial" && activeSerialWriter) {
    await activeSerialWriter.write(rawBytes);
  }

  // Dispatch into Message Stream Monitor
  StreamMonitor.injectTxPacket(pkt);
}

function downloadBinaryFile() {
  if (!lastCompiledData || !lastCompiledData.packets) {
    flashToast("Compile program first");
    return;
  }

  let totalBytes = 0;
  lastCompiledData.packets.forEach(p => {
    totalBytes += 2 + (p.bytes ? p.bytes.length : p.size);
  });

  const bundle = new Uint8Array(totalBytes);
  let off = 0;
  lastCompiledData.packets.forEach(p => {
    const raw = p.bytes ? new Uint8Array(p.bytes) : (p.raw || new Uint8Array(0));
    bundle[off++] = raw.length & 0xFF;
    bundle[off++] = (raw.length >> 8) & 0xFF;
    bundle.set(raw, off);
    off += raw.length;
  });

  const blob = new Blob([bundle], { type: "application/octet-stream" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `runit_compiled_program_${Date.now()}.bin`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
  flashToast(`Downloaded binary program bundle (${totalBytes} bytes)`);
}

function copyCArray() {
  if (!lastCompiledData || !lastCompiledData.packets) {
    flashToast("Compile program first");
    return;
  }

  let code = `// =========================================================================\n`;
  code += `// runIT Studio Compiled Wire Packets (Class 0x04 VM Loader)\n`;
  code += `// Total Packets: ${lastCompiledData.packet_count}, Total Arena: ${lastCompiledData.total_arena_size} bytes\n`;
  code += `// =========================================================================\n\n`;

  lastCompiledData.packets.forEach(p => {
    const bytes = p.bytes || Array.from(p.raw || []);
    const hexList = bytes.map(b => "0x" + ("00" + b.toString(16).toUpperCase()).slice(-2)).join(", ");
    code += `// Packet #${p.idx}: ${p.name} (${p.size} bytes)\n`;
    code += `static const uint8_t vm_pkt_${p.idx}[] = { ${hexList} };\n\n`;
  });

  code += `static const struct { const uint8_t* data; size_t len; } vm_program_bundle[] = {\n`;
  lastCompiledData.packets.forEach(p => {
    code += `  { vm_pkt_${p.idx}, sizeof(vm_pkt_${p.idx}) },\n`;
  });
  code += `};\n`;

  navigator.clipboard?.writeText(code);
  flashToast("C Array copied to clipboard");
}

function copyHexStream() {
  if (!lastCompiledData || !lastCompiledData.packets) {
    flashToast("Compile program first");
    return;
  }

  const text = lastCompiledData.packets.map(p => p.hex).join("\n");
  navigator.clipboard?.writeText(text);
  flashToast("All packet hex streams copied to clipboard");
}

async function connectWebBluetooth() {
  if (!navigator.bluetooth) {
    throw new Error("Web Bluetooth not supported by this browser (use Chrome or Edge)");
  }
  const device = await navigator.bluetooth.requestDevice({
    acceptAllDevices: true,
    optionalServices: [
      "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
      "0000ffe0-0000-1000-8000-00805f9b34fb"
    ]
  });
  const server = await device.gatt.connect();
  activeBleDevice = device;

  let service = null;
  try {
    service = await server.getPrimaryService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    activeBleCharacteristic = await service.getCharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
  } catch (e) {
    const services = await server.getPrimaryServices();
    if (services.length > 0) {
      const chars = await services[0].getCharacteristics();
      activeBleCharacteristic = chars.find(c => c.properties.write || c.properties.writeWithoutResponse) || chars[0];
    }
  }

  if (!activeBleCharacteristic) {
    throw new Error("Connected to BLE device but no writable characteristic found");
  }

  flashToast(`Connected to BLE device: ${device.name || 'ESP32'}`);
}

async function connectWebSerial() {
  if (!navigator.serial) {
    throw new Error("Web Serial not supported by this browser (use Chrome or Edge)");
  }
  const port = await navigator.serial.requestPort();
  await port.open({ baudRate: 115200 });
  activeSerialPort = port;
  activeSerialWriter = port.writable.getWriter();
  flashToast("Connected to USB Serial port at 115200 baud");
}

// ==========================================
// 9. EVENT LISTENERS SETUP
// ==========================================

document.addEventListener("DOMContentLoaded", () => {
  // Try restoring from LocalStorage first; if not present, load master preset
  if (!loadFromLocalStorage()) {
    loadPreset("master");
  }

  // Preset selector
  document.getElementById("presetSelect")?.addEventListener("change", (e) => {
    if (e.target.value) {
      loadPreset(e.target.value);
    }
  });

  // Reload Master button
  document.getElementById("btnReloadMaster")?.addEventListener("click", () => {
    loadPreset("master");
    flashToast("Reset to master industrial robot cell preset");
  });

  // Filter input
  document.getElementById("filterInput")?.addEventListener("input", () => {
    renderTree();
  });

  // Expand / Collapse all
  document.getElementById("btnExpandAll")?.addEventListener("click", () => {
    variableNodes.forEach(n => n.collapsed = false);
    renderTree();
  });

  document.getElementById("btnCollapseAll")?.addEventListener("click", () => {
    variableNodes.forEach(n => n.collapsed = true);
    renderTree();
  });

  // Add Root Tree (PTR) & Add Root Variable buttons
  document.getElementById("btnAddRootTree")?.addEventListener("click", addRootTree);
  document.getElementById("btnAddRootVar")?.addEventListener("click", addRootVar);

  // File Upload & Export listeners
  const fileUploadInput = document.getElementById("fileUploadInput");
  const btnUploadJson = document.getElementById("btnUploadJson");
  const btnDownloadJson = document.getElementById("btnDownloadJson");

  btnUploadJson?.addEventListener("click", () => {
    fileUploadInput?.click();
  });

  fileUploadInput?.addEventListener("change", handleFileSelect);
  btnDownloadJson?.addEventListener("click", downloadJsonFile);

  // Full-Screen Drag and Drop
  initDragAndDrop();

  // Live Runtime Telemetry Monitor toggle
  document.getElementById("btnToggleLive")?.addEventListener("click", toggleLiveMode);

  // JSON View handlers
  document.getElementById("btnViewJson")?.addEventListener("click", openJsonModal);
  document.getElementById("btnCloseJson")?.addEventListener("click", () => {
    document.getElementById("jsonModal").classList.add("hidden");
  });
  document.getElementById("btnCopyJson")?.addEventListener("click", () => {
    const text = document.getElementById("jsonEditorArea").value;
    navigator.clipboard?.writeText(text);
    flashToast("JSON copied to clipboard");
  });
  document.getElementById("btnApplyJson")?.addEventListener("click", applyJsonToTree);

  // Compile handlers
  document.getElementById("btnCompile")?.addEventListener("click", compileProgram);
  document.getElementById("btnCloseCompile")?.addEventListener("click", () => {
    document.getElementById("compileModal").classList.add("hidden");
  });

  // Compile transmission & export action handlers (Ready to be Sent)
  document.getElementById("btnSendAllPackets")?.addEventListener("click", sendAllPackets);
  document.getElementById("btnDownloadBin")?.addEventListener("click", downloadBinaryFile);
  document.getElementById("btnCopyCArray")?.addEventListener("click", copyCArray);
  document.getElementById("btnCopyHexStream")?.addEventListener("click", copyHexStream);

  // Packet list individual actions (Send single packet, copy single hex)
  document.getElementById("packetList")?.addEventListener("click", (e) => {
    const sendBtn = e.target.closest(".btn-pkt-send");
    if (sendBtn) {
      const idx = parseInt(sendBtn.dataset.idx, 10);
      if (idx) sendSinglePacket(idx);
      return;
    }

    const copyBtn = e.target.closest(".btn-pkt-copy");
    if (copyBtn) {
      const idx = parseInt(copyBtn.dataset.idx, 10);
      if (idx && lastCompiledData && lastCompiledData.packets) {
        const pkt = lastCompiledData.packets.find(p => p.idx === idx);
        if (pkt) {
          navigator.clipboard?.writeText(pkt.hex);
          flashToast(`Copied hex for Packet #${pkt.idx}`);
        }
      }
    }
  });

  // 2D Matrix (PTR Style) Modal handlers
  document.getElementById("btnAddPtrMatrix")?.addEventListener("click", () => {
    openMatrixModal(null);
  });
  document.getElementById("btnCloseMatrix")?.addEventListener("click", closeMatrixModal);
  document.getElementById("btnCancelMatrix")?.addEventListener("click", closeMatrixModal);
  document.getElementById("btnCreateMatrixConfirm")?.addEventListener("click", createMatrixFromModal);

  // Live Matrix Preview triggers
  ["matrixNameInput", "matrixRowsInput", "matrixColsInput", "matrixTypeSelect", "matrixDefaultVal"].forEach(id => {
    const el = document.getElementById(id);
    if (el) {
      el.addEventListener("input", updateMatrixPreview);
      el.addEventListener("change", updateMatrixPreview);
    }
  });

  document.getElementById("matrixModal")?.addEventListener("click", (e) => {
    if (e.target.id === "matrixModal") {
      closeMatrixModal();
    }
  });

  // Initialize Message Stream Monitor & Decoder
  if (typeof StreamMonitor !== "undefined" && typeof StreamMonitor.init === "function") {
    StreamMonitor.init();
  }
});

// ==========================================
// 9. 2D MATRIX CREATOR (PTR RELATIONAL STYLE)
// ==========================================

function openMatrixModal(parentId = null) {
  const modal = document.getElementById("matrixModal");
  if (!modal) return;
  document.getElementById("matrixParentId").value = parentId ? String(parentId) : "";
  document.getElementById("matrixNameInput").value = parentId ? "nested_matrix" : "waypoint_matrix";
  document.getElementById("matrixRowsInput").value = "3";
  document.getElementById("matrixColsInput").value = "4";
  document.getElementById("matrixTypeSelect").value = "F";
  document.getElementById("matrixDefaultVal").value = "0.0";
  updateMatrixPreview();
  modal.classList.remove("hidden");
  document.getElementById("matrixNameInput")?.focus();
}

function closeMatrixModal() {
  document.getElementById("matrixModal")?.classList.add("hidden");
}

function updateMatrixPreview() {
  const name = document.getElementById("matrixNameInput")?.value.trim() || "matrix";
  const rows = Math.min(64, Math.max(1, parseInt(document.getElementById("matrixRowsInput")?.value, 10) || 1));
  const cols = Math.min(64, Math.max(1, parseInt(document.getElementById("matrixColsInput")?.value, 10) || 1));
  const type = document.getElementById("matrixTypeSelect")?.value || "F";
  const defVal = document.getElementById("matrixDefaultVal")?.value.trim() || "0.0";

  const previewEl = document.getElementById("matrixPreview");
  if (!previewEl) return;

  const sampleRow = Array(Math.min(cols, 4)).fill(defVal).join(", ") + (cols > 4 ? ", ..." : "");
  let lines = [];
  lines.push(`└── [PTR] ${name} (${rows} rows × ${cols} cols = ${rows * cols} total slots)`);
  for (let r = 0; r < Math.min(rows, 4); r++) {
    const isLast = (r === rows - 1) || (r === 3 && rows > 4);
    const branch = isLast ? "    └──" : "    ├──";
    lines.push(`${branch} row_${r}: ${type}[${cols}] = [ ${sampleRow} ]`);
  }
  if (rows > 4) {
    lines.push(`    └── ... (${rows - 4} more row arrays)`);
  }
  lines.push(``);
  lines.push(`→ Accessor syntax: ${name}[row][col] (chained dereference)`);

  previewEl.textContent = lines.join("\n");
}

function createMatrixFromModal() {
  const parentIdVal = document.getElementById("matrixParentId")?.value;
  const parentId = parentIdVal ? parseInt(parentIdVal, 10) : null;
  const name = document.getElementById("matrixNameInput")?.value.trim() || "matrix_grid";
  const rows = Math.min(64, Math.max(1, parseInt(document.getElementById("matrixRowsInput")?.value, 10) || 1));
  const cols = Math.min(64, Math.max(1, parseInt(document.getElementById("matrixColsInput")?.value, 10) || 1));
  const type = document.getElementById("matrixTypeSelect")?.value || "F";
  const defVal = document.getElementById("matrixDefaultVal")?.value.trim() || "0.0";

  // 1. Create Parent PTR container
  const parentContainer = {
    id: nextNodeId++,
    name: name,
    type: "PTR",
    count: rows,
    initial: "",
    parentId: parentId,
    linkToId: null,
    mutable: true,
    subscribe: false,
    collapsed: false
  };
  variableNodes.push(parentContainer);

  // If parentId was provided, ensure that parent is expanded
  if (parentId) {
    const parent = getNodeById(parentId);
    if (parent) parent.collapsed = false;
  }

  // 2. Create M child row arrays
  for (let r = 0; r < rows; r++) {
    const rowValues = Array(cols).fill(defVal).join(", ");
    const rowObj = {
      id: nextNodeId++,
      name: `row_${r}`,
      type: type,
      count: cols,
      initial: rowValues,
      parentId: parentContainer.id,
      linkToId: null,
      mutable: true,
      subscribe: false,
      collapsed: false
    };
    variableNodes.push(rowObj);
  }

  closeMatrixModal();
  renderTree();

  setTimeout(() => {
    jumpToNode(parentContainer.id);
  }, 100);
}
