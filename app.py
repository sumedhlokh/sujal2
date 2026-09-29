"""
AgriBot Mission Control - Streamlit front end

IMPORTANT: This file does NOT simulate anything itself.
Every button click below calls the compiled C++ program
(agribot_core.exe) as a subprocess. The C++ program owns the
grid, robot position, battery, undo/redo stacks and mission
queue; this file only sends it an action and displays the
JSON it returns.

Folder layout expected (all in one folder):
    app.py                (this file)
    agribot_core.exe       (Windows)   or   agribot_core   (Linux/Mac)
    agribot_state.txt      (created automatically on first run)

Run with:
    pip install streamlit
    streamlit run app.py
"""
import json
import os
import subprocess

import streamlit as st

st.set_page_config(page_title="AgriBot Mission Control", page_icon="🌾", layout="wide")

HERE = os.path.dirname(os.path.abspath(__file__))
STATE_FILE = os.path.join(HERE, "agribot_state.txt")
EXE_NAME = "agribot_core.exe" if os.name == "nt" else "agribot_core"
EXE_PATH = os.path.join(HERE, EXE_NAME)
CPP_PATH = os.path.join(HERE, "agribot_core.cpp")


def ensure_exe_built():
    """If the compiled program is missing (e.g. a fresh clone on Streamlit
    Cloud that has never been built), compile it automatically from the
    .cpp file that lives in the same repo. Safe to call on every run:
    it does nothing once the executable already exists."""
    if os.path.exists(EXE_PATH):
        return True
    if not os.path.exists(CPP_PATH):
        return False
    try:
        result = subprocess.run(
            ["g++", "-std=c++17", CPP_PATH, "-o", EXE_PATH],
            capture_output=True, text=True, timeout=60,
        )
        if result.returncode != 0:
            st.error("Auto-compile of agribot_core.cpp failed:\n" + result.stderr)
            return False
        if os.name != "nt":
            os.chmod(EXE_PATH, 0o755)   # mark it runnable on Linux/Mac servers
        return True
    except FileNotFoundError:
        # g++ itself isn't installed on this machine
        return False
    except Exception as e:
        st.error(f"Auto-compile error: {e}")
        return False


ensure_exe_built()


def call_core(action, **kwargs):
    """Run the C++ core for exactly one action and return its parsed JSON state."""
    if not os.path.exists(EXE_PATH):
        st.error(
            f"Cannot find or build {EXE_NAME} next to app.py.\n\n"
            "If running locally: compile it yourself with\n"
            f"  g++ -std=c++17 agribot_core.cpp -o {EXE_NAME}\n\n"
            "If deployed on Streamlit Cloud: add a file named 'packages.txt' "
            "to your GitHub repo (same folder as app.py) containing one line: g++ "
            "— this tells the server to install a C++ compiler so the app can "
            "build agribot_core.cpp automatically on first run."
        )
        st.stop()

    args = [EXE_PATH, STATE_FILE, action] + [f"{k}={v}" for k, v in kwargs.items()]
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=5)
    except Exception as e:
        st.error(f"Could not run the C++ program: {e}")
        st.stop()

    if not result.stdout.strip():
        st.error("The C++ program produced no output. stderr was:\n" + result.stderr)
        st.stop()

    try:
        return json.loads(result.stdout.strip())
    except json.JSONDecodeError:
        st.error("The C++ program's output wasn't valid JSON:\n" + result.stdout)
        st.stop()


# ---- Load current state (a cheap "state" call, no changes made) ----
if "state" not in st.session_state:
    st.session_state.state = call_core("state")

state = st.session_state.state

st.title("AgriBot Mission Control")
st.caption("Streamlit is only the display. Every action below runs the compiled C++ program.")

CELL_COLOR = {"H": "#14311f", "W": "#4a3b12", "D": "#4a1414"}
CELL_LABEL = {"H": "Healthy", "W": "Weed", "D": "Disease"}

left, right = st.columns([1.4, 1])

with left:
    st.subheader("Field View")
    for i in range(5):
        cols = st.columns(5)
        for j in range(5):
            ch = state["grid"][i][j]
            is_robot = (state["x"] == i and state["y"] == j)
            bg = CELL_COLOR[ch]
            text = "R" if is_robot else ""
            cols[j].markdown(
                f"<div style='background:{bg};border-radius:8px;height:64px;"
                f"display:flex;align-items:center;justify-content:center;"
                f"font-size:22px;font-weight:bold;color:#eee;position:relative'>"
                f"{text}<span style='position:absolute;bottom:2px;right:6px;"
                f"font-size:9px;color:#9db3a6'>{i},{j}</span></div>",
                unsafe_allow_html=True,
            )
    st.markdown(
        " &nbsp; ".join(f"<span style='color:#9db3a6'>{v}</span>" for v in CELL_LABEL.values()),
        unsafe_allow_html=True,
    )

    if state.get("msg"):
        st.info(state["msg"])

    st.subheader("Robot Controls")
    c1, c2, c3 = st.columns(3)
    if c1.button("Up", use_container_width=True):
        st.session_state.state = call_core("move", dir="up")
        st.rerun()
    if c2.button("Inspect", use_container_width=True):
        st.session_state.state = call_core("inspect")
        st.rerun()
    if c3.button("Undo", use_container_width=True):
        st.session_state.state = call_core("undo")
        st.rerun()

    c4, c5, c6 = st.columns(3)
    if c4.button("Left", use_container_width=True):
        st.session_state.state = call_core("move", dir="left")
        st.rerun()
    if c5.button("Down", use_container_width=True):
        st.session_state.state = call_core("move", dir="down")
        st.rerun()
    if c6.button("Right", use_container_width=True):
        st.session_state.state = call_core("move", dir="right")
        st.rerun()

    c7, c8 = st.columns(2)
    if c7.button("Spray", use_container_width=True):
        st.session_state.state = call_core("spray")
        st.rerun()
    if c8.button("Redo", use_container_width=True):
        st.session_state.state = call_core("redo")
        st.rerun()

    if st.button("Reset mission", use_container_width=True):
        st.session_state.state = call_core("reset")
        st.rerun()

with right:
    st.subheader("Robot Status")
    st.metric("Battery", f"{state['battery']}%")
    st.progress(state["battery"] / 100)
    st.metric("Position", f"({state['x']}, {state['y']})")

    st.subheader("Mission Queue (Queue)")
    qtype = st.selectbox("Mission type", ["move", "inspect", "spray"])
    qdir = None
    if qtype == "move":
        qdir = st.selectbox("Direction", ["up", "down", "left", "right"])

    cq1, cq2, cq3 = st.columns(3)
    if cq1.button("Add", use_container_width=True):
        kwargs = {"type": qtype}
        if qdir:
            kwargs["dir"] = qdir
        st.session_state.state = call_core("queue_add", **kwargs)
        st.rerun()
    if cq2.button("Run Next", use_container_width=True):
        st.session_state.state = call_core("queue_next")
        st.rerun()
    if cq3.button("Run All", use_container_width=True):
        st.session_state.state = call_core("queue_all")
        st.rerun()

    if state["queue"]:
        for i, m in enumerate(state["queue"], 1):
            st.write(f"{i}. {m}")
    else:
        st.caption("No missions queued.")

    st.subheader("Undo Stack")
    if state["undo"]:
        for m in state["undo"]:
            st.write("<-", m)
    else:
        st.caption("empty")

    st.subheader("Redo Stack")
    if state["redo"]:
        for m in state["redo"]:
            st.write("->", m)
    else:
        st.caption("empty")

    st.subheader("Mission History (Vector)")
    if state["history"]:
        for h in state["history"]:
            st.caption(h)
    else:
        st.caption("No actions yet.")

st.divider()
st.caption(
    "The field, robot position, battery, undo/redo stacks and mission queue all "
    "live inside the compiled C++ program (agribot_core.exe). This page only "
    "sends it one action per click and shows what it returns."
)
