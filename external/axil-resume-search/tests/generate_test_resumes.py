#!/usr/bin/env python3
import os
import sys

class PDFCanvas:
    """
    Lightweight, dependency-free PDF generator that outputs visually rich,
    well-structured 1-page A4 PDFs matching the typography and color palette
    of ~/cv/style.mom.
    """
    def __init__(self, width=595.28, height=841.89):
        self.width = width
        self.height = height
        self.stream = []

    def set_fill_rgb(self, r, g, b):
        self.stream.append(f"{r:.3f} {g:.3f} {b:.3f} rg\n")

    def set_stroke_rgb(self, r, g, b):
        self.stream.append(f"{r:.3f} {g:.3f} {b:.3f} RG\n")

    def set_line_width(self, w):
        self.stream.append(f"{w:.2f} w\n")

    def line(self, x1, y1, x2, y2):
        self.stream.append(f"{x1:.2f} {y1:.2f} m {x2:.2f} {y2:.2f} l S\n")

    def rect(self, x, y, w, h, fill=True, stroke=False):
        op = "B\n" if (fill and stroke) else ("f\n" if fill else "S\n")
        self.stream.append(f"{x:.2f} {y:.2f} {w:.2f} {h:.2f} re {op}")

    def text(self, font, size, x, y, text_str, r=None, g=None, b=None):
        self.stream.append("BT\n")
        if r is not None and g is not None and b is not None:
            self.stream.append(f"{r:.3f} {g:.3f} {b:.3f} rg\n")
        self.stream.append(f"/{font} {size} Tf\n")
        self.stream.append(f"{x:.2f} {y:.2f} Td\n")
        escaped = text_str.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
        self.stream.append(f"({escaped}) Tj\n")
        self.stream.append("ET\n")

    def wrap_lines(self, font, size, text, max_width):
        avg_char_w = size * 0.49
        max_chars = max(15, int(max_width / avg_char_w))
        words = text.split()
        lines = []
        cur = []
        cur_len = 0
        for w in words:
            if cur_len + len(w) + 1 <= max_chars:
                cur.append(w)
                cur_len += len(w) + 1
            else:
                if cur:
                    lines.append(" ".join(cur))
                cur = [w]
                cur_len = len(w)
        if cur:
            lines.append(" ".join(cur))
        return lines

    def to_bytes(self):
        content = "".join(self.stream).encode("latin1", errors="replace")
        slen = len(content)

        pdf = bytearray()
        pdf.extend(b"%PDF-1.4\n")

        objs = []
        def add_obj(data):
            off = len(pdf)
            objs.append(off)
            pdf.extend(f"{len(objs)} 0 obj\n".encode("latin1"))
            pdf.extend(data)
            pdf.extend(b"\nendobj\n")
            return len(objs)

        add_obj(b"<< /Type /Catalog /Pages 2 0 R >>")
        add_obj(b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
        add_obj(f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {self.width:.2f} {self.height:.2f}] /Contents 4 0 R /Resources << /Font << /F1 5 0 R /F2 6 0 R /F3 7 0 R >> >> >>".encode("latin1"))
        add_obj(f"<< /Length {slen} >>\nstream\n".encode("latin1") + content + b"\nendstream")
        add_obj(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
        add_obj(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>")
        add_obj(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique >>")

        xref_off = len(pdf)
        pdf.extend(f"xref\n0 {len(objs)+1}\n0000000000 65535 f \n".encode("latin1"))
        for off in objs:
            pdf.extend(f"{off:010d} 00000 n \n".encode("latin1"))
        pdf.extend(f"trailer\n<< /Size {len(objs)+1} /Root 1 0 R >>\nstartxref\n{xref_off}\n%%EOF\n".encode("latin1"))
        return bytes(pdf)


def render_full_page_resume(data, out_path):
    """
    Renders a comprehensive, dense 1-page A4 resume following ~/cv/style.mom conventions:
      Page: 595.28 x 841.89 pt (A4)
      Colors:
        Navy:  #17263C (0.090, 0.149, 0.235)
        Slate: #1E293B (0.118, 0.161, 0.231)
        Teal:  #0D9488 (0.051, 0.580, 0.533)
        Faint: #64748B (0.392, 0.455, 0.545)
        Pill:  #EBF4F3 (0.922, 0.957, 0.953)
    """
    c = PDFCanvas(width=595.28, height=841.89)

    c_navy = (0.090, 0.149, 0.235)
    c_slate = (0.118, 0.161, 0.231)
    c_teal = (0.051, 0.580, 0.533)
    c_faint = (0.392, 0.455, 0.545)
    c_pill = (0.922, 0.957, 0.953)

    margin_x = 42.0
    full_w = 595.28 - 2 * margin_x  # 511.28 pt

    # 1. Header
    y = 812.0
    c.text("F2", 18, margin_x, y, data["name"], *c_navy)

    y -= 14.5
    c.text("F2", 10, margin_x, y, data["title"], *c_slate)

    y -= 11.5
    c.text("F3", 8.5, margin_x, y, data["tagline"], *c_teal)

    y -= 11.0
    contact_str = f"{data['location']}   |   {data['phone']}   |   {data['email']}   |   {data['github']}"
    c.text("F1", 7.8, margin_x, y, contact_str, *c_faint)

    # Accent rule
    y -= 6.0
    c.set_stroke_rgb(*c_teal)
    c.set_line_width(1.2)
    c.line(margin_x, y, margin_x + full_w, y)

    # 2. Profile Callout Box
    y -= 10.0
    prof_lines = []
    for p in data["profile"]:
        prof_lines.extend(c.wrap_lines("F1", 8.0, p, full_w - 18))

    box_h = len(prof_lines) * 9.8 + 18
    box_y = y - box_h
    c.set_fill_rgb(*c_pill)
    c.set_stroke_rgb(*c_teal)
    c.set_line_width(0.6)
    c.rect(margin_x, box_y, full_w, box_h, fill=True, stroke=True)

    c.text("F2", 8.5, margin_x + 9, y - 10, "PROFILE", *c_navy)
    cur_py = y - 20
    for pl in prof_lines:
        c.text("F1", 8.0, margin_x + 9, cur_py, pl, *c_slate)
        cur_py -= 9.8

    # 3. Two-Column Layout
    y = box_y - 12.0
    col_gap = 14.0
    left_w = 282.0
    right_w = full_w - left_w - col_gap  # ~215 pt
    right_x = margin_x + left_w + col_gap

    # Left Column: EXPERIENCE
    ly = y
    c.text("F2", 9.5, margin_x, ly, "EXPERIENCE", *c_navy)
    c.set_stroke_rgb(*c_teal)
    c.set_line_width(0.8)
    c.line(margin_x, ly - 2.5, margin_x + left_w, ly - 2.5)
    ly -= 12.0

    for exp in data["experience"]:
        # Role Header: Title and Dates
        c.text("F2", 8.4, margin_x, ly, exp["role"], *c_navy)
        c.text("F1", 7.4, margin_x + left_w - 70, ly, exp["dates"], *c_faint)
        ly -= 9.2

        # Company & Location
        comp_loc = f"{exp['company']}   -   {exp['location']}"
        c.text("F3", 7.6, margin_x, ly, comp_loc, *c_teal)
        ly -= 9.2

        # Description lines
        for desc_p in exp["points"]:
            dlines = c.wrap_lines("F1", 7.4, desc_p, left_w)
            for dl in dlines:
                c.text("F1", 7.4, margin_x, ly, dl, *c_slate)
                ly -= 8.8

        # Stack tag line
        if "stack" in exp and exp["stack"]:
            st_lines = c.wrap_lines("F1", 7.0, "Stack: " + exp["stack"], left_w)
            for stl in st_lines:
                c.text("F2" if stl.startswith("Stack:") else "F1", 7.0, margin_x, ly, stl, *c_teal)
                ly -= 8.0
        ly -= 5.5

    # Right Column: SKILLS, EDUCATION, LANGUAGES, ACHIEVEMENTS
    ry = y
    c.text("F2", 9.5, right_x, ry, "CORE SKILLS", *c_navy)
    c.set_stroke_rgb(*c_teal)
    c.set_line_width(0.8)
    c.line(right_x, ry - 2.5, right_x + right_w, ry - 2.5)
    ry -= 12.0

    for group in data["skill_groups"]:
        c.text("F2", 8.2, right_x, ry, group["group"], *c_navy)
        ry -= 9.5
        # Skill pill tags
        cur_tag_line = []
        cur_tag_len = 0
        for sk in group["skills"]:
            pill_str = f"[{sk}]"
            if cur_tag_len + len(pill_str) + 1 > int(right_w / 4.2):
                c.text("F1", 7.0, right_x, ry, " ".join(cur_tag_line), *c_teal)
                ry -= 8.2
                cur_tag_line = [pill_str]
                cur_tag_len = len(pill_str)
            else:
                cur_tag_line.append(pill_str)
                cur_tag_len += len(pill_str) + 1
        if cur_tag_line:
            c.text("F1", 7.0, right_x, ry, " ".join(cur_tag_line), *c_teal)
            ry -= 10.0

    # Key Projects / Stack Architecture in Right Column
    if "projects" in data and data["projects"]:
        ry -= 4.0
        c.text("F2", 9.5, right_x, ry, "SYSTEMS ARCHITECTURE & PROJECTS", *c_navy)
        c.set_stroke_rgb(*c_teal)
        c.set_line_width(0.8)
        c.line(right_x, ry - 2.5, right_x + right_w, ry - 2.5)
        ry -= 12.0

        for ptitle, pdesc in data["projects"]:
            c.text("F2", 7.8, right_x, ry, ptitle, *c_navy)
            ry -= 8.5
            p_lines = c.wrap_lines("F1", 7.0, pdesc, right_w)
            for pl in p_lines:
                c.text("F1", 7.0, right_x, ry, pl, *c_slate)
                ry -= 8.0
            ry -= 3.5

    # Education in right column
    ry -= 4.0
    c.text("F2", 9.5, right_x, ry, "EDUCATION", *c_navy)
    c.set_stroke_rgb(*c_teal)
    c.set_line_width(0.8)
    c.line(right_x, ry - 2.5, right_x + right_w, ry - 2.5)
    ry -= 12.0

    for edu in data["education"]:
        c.text("F2", 7.8, right_x, ry, edu["degree"], *c_navy)
        ry -= 8.5
        c.text("F1", 7.2, right_x, ry, edu["school"], *c_slate)
        ry -= 9.5

    # Languages
    if "languages" in data:
        ry -= 3.0
        c.text("F2", 9.5, right_x, ry, "LANGUAGES", *c_navy)
        c.set_stroke_rgb(*c_teal)
        c.set_line_width(0.8)
        c.line(right_x, ry - 2.5, right_x + right_w, ry - 2.5)
        ry -= 12.0
        for lang, lev in data["languages"]:
            c.text("F2", 7.5, right_x, ry, lang, *c_navy)
            c.text("F1", 7.2, right_x + 65, ry, lev, *c_faint)
            ry -= 8.5

    # Write PDF file
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(c.to_bytes())


def build_all(output_dir):
    # 1. Paulo Quirino (Full 8-role career from cv.mom)
    render_full_page_resume({
        "name": "Paulo Quirino",
        "title": "Lead Frontend Engineer & Systems Architect",
        "tagline": "Specialist in React, TypeScript, Component Systems & High-Performance Web Engines",
        "location": "Lisbon, Portugal",
        "phone": "+351 966211859",
        "email": "quirinpa@gmail.com",
        "github": "github.com/tty-pt",
        "profile": [
            "Bridges low-level mechanical sympathy (C99, WASM, POSIX event loops) with modern reactive UI ergonomics (React, TypeScript, state orchestration). 8+ years delivering high-scale web platforms, real-time robotics telemetry interfaces, and datatype-driven component systems with deterministic performance and minimal operational overhead."
        ],
        "experience": [
            {
                "role": "Front-end & Architecture Specialist",
                "company": "Loomtech",
                "location": "Lisbon",
                "dates": "11/2025 - 08/2026",
                "points": ["Architected frontend and data model for a block-based document editor engine supporting drag-and-drop layouts, dynamic formatting, and PDF export. Engineered datatype-driven UI framework automating component reuse across 4 core entities."],
                "stack": "React, TypeScript, Valtio, React-Query, Zod, TailwindCSS, Vite"
            },
            {
                "role": "Lead Frontend Developer",
                "company": "Mov.ai",
                "location": "Lisbon",
                "dates": "11/2022 - 02/2025",
                "points": ["Primary frontend engineer for autonomous robotics solution. Built 3D interactive scenes in BabylonJS, a visual node flow behavior editor, and multi-type code editor for robotics coordination. Developed real-time telemetry tables and centralized log controller."],
                "stack": "React 18, TypeScript, BabylonJS, Material-UI 6, Remix, Vite, esbuild"
            },
            {
                "role": "Front-end Developer / Tech Lead",
                "company": "PrimeIT",
                "location": "Lisbon",
                "dates": "12/2021 - 07/2022",
                "points": ["Tech lead and frontend engineer architecting micro-frontends for enterprise GDPR compliance platform. Pioneered datatype-driven UI architecture and pure-stack design principles, formalizing data models."],
                "stack": "React, Micro-Frontends, React-Router, TypeScript, SCRUM"
            },
            {
                "role": "Full-stack Developer",
                "company": "UNPARALLEL",
                "location": "Lisbon",
                "dates": "10/2020 - 11/2021",
                "points": ["Delivered full-stack applications in Next.js. Integrated IoT telemetry with a custom C++ Arduino library for acoustic sensors and authored JavaScript utility libraries."],
                "stack": "Next.js, React, TypeScript, C++, WordPress, Node.js"
            },
            {
                "role": "Full-stack Developer",
                "company": "Mobinteg",
                "location": "Lisbon",
                "dates": "11/2019 - 05/2020",
                "points": ["Architected an interoperable runtime launcher executing seamlessly across Angular and Mithril. Engineered an early utility-first CSS compilation engine predating modern Tailwind paradigms."],
                "stack": "Angular, Mithril, TypeScript, Webpack, Babel, PHP, MySQL"
            },
            {
                "role": "Web & Mobile Developer",
                "company": "Exictos",
                "location": "Funchal",
                "dates": "08/2017 - 08/2018",
                "points": ["Maintained core banking employee portals and engineered cross-platform mobile clients with React Native and Ionic. Authored C tool for deduplicating localization assets."],
                "stack": "React Native, Ionic, JavaScript, C, Spring Boot"
            }
        ],
        "skill_groups": [
            {"group": "Frontend Architecture & UI", "skills": ["React", "TypeScript", "Next.js", "Micro-Frontends", "React-Query", "Zod", "Valtio", "TailwindCSS", "Material-UI", "Redux", "BabylonJS (3D)", "Component Reuse", "Vite"]},
            {"group": "Systems, C & Architecture", "skills": ["C (C99/C11)", "C++", "Systems Architecture", "POSIX/Unix", "Isomorphic WASM", "In-Memory DBs", "Full-Text Search", "Data Modelling"]},
            {"group": "Backend & Databases", "skills": ["Node.js", "PostgreSQL", "Redis", "MongoDB", "BerkeleyDB", "MySQL", "PHP"]},
            {"group": "AI, Tooling & DevOps", "skills": ["Python", "Vulkan/Local LLMs", "AI Workflows", "Linux & DevOps", "Docker", "Github CI"]}
        ],
        "projects": [
            ("axil HTTP/WS Server", "Event-driven HTTP(S) and WebSocket daemon in C99. Non-blocking I/O loop, zero dependencies."),
            ("libxylem Dynamic Module System", "Hook-based plugin kernel with shared library DAG loading, lifecycle hooks, and ABI boundaries.")
        ],
        "education": [{"degree": "Computer Science & Systems Engineering", "school": "University of Madeira"}],
        "languages": [("Portuguese", "Native / C2"), ("English", "Fluent / C1/C2")]
    }, os.path.join(output_dir, "paulo_lead_frontend_systems_architect.pdf"))

    # 2. Elena Rostova
    render_full_page_resume({
        "name": "Elena Rostova",
        "title": "Senior Distributed Systems & Storage Engine Engineer",
        "tagline": "Specialist in Asynchronous Rust, Raft Consensus, LSM-Tree Internals & io_uring",
        "location": "Stockholm, Sweden",
        "phone": "+46 8 555 3120",
        "email": "elena.rostova@bytescale.io",
        "github": "github.com/erostova",
        "profile": [
            "Distributed systems engineer with 8+ years specializing in high-throughput transactional storage engines, write-ahead logging (WAL), and LSM-tree architectures. Deep expertise in asynchronous Rust (Tokio), Raft consensus protocols, lock-free concurrency, and zero-copy Linux asynchronous I/O using io_uring."
        ],
        "experience": [
            {
                "role": "Principal Storage Engineer",
                "company": "ByteScale Systems",
                "location": "Stockholm",
                "dates": "03/2021 - Present",
                "points": ["Architected distributed key-value storage engine in Rust with multi-Raft partition groups. Integrated Linux io_uring for non-blocking disk writes, sustaining 2.5 million sustained IOPS per storage node with p99 tail latency under 0.8ms."],
                "stack": "Rust, Tokio, io_uring, Raft, RocksDB, gRPC, Protobuf, eBPF"
            },
            {
                "role": "Senior Distributed Systems Engineer",
                "company": "CloudData Labs",
                "location": "Stockholm",
                "dates": "06/2018 - 02/2021",
                "points": ["Engineered multi-group Raft partition coordinator for distributed database. Optimized LSM compaction pipelines and block cache eviction policies, reducing write amplification factor from 18x to 5.2x."],
                "stack": "Rust, C++, LSM-Trees, Write-Ahead Logging, Paxos, Zookeeper"
            },
            {
                "role": "Systems Programmer",
                "company": "Nordic Infrastructure",
                "location": "Gothenburg",
                "dates": "09/2016 - 05/2018",
                "points": ["Authored high-performance network file storage servers in C and Rust with custom slab memory allocators and zero-copy sendfile transfers."],
                "stack": "C99, Rust, POSIX Sockets, Epoll, Linux Kernel, GDB"
            },
            {
                "role": "Database Intern",
                "company": "Ericsson Research",
                "location": "Kista",
                "dates": "06/2015 - 08/2016",
                "points": ["Benchmarked lock-free B-tree concurrent indexing algorithms under high write contention workloads in C++."],
                "stack": "C++, Multithreading, Atomics, Valgrind, Perf"
            }
        ],
        "skill_groups": [
            {"group": "Distributed Systems", "skills": ["Raft Consensus", "Paxos", "Multi-Raft", "Distributed Locking", "Failure Detectors", "Split-Brain Prevention"]},
            {"group": "Storage & Linux I/O", "skills": ["LSM-Trees", "Write-Ahead Logging", "io_uring", "Direct I/O", "mmap", "eBPF", "RocksDB", "SSTables"]},
            {"group": "Languages & Frameworks", "skills": ["Rust", "Tokio", "C (C99)", "C++17/20", "gRPC", "Protobuf", "Flamegraphs"]},
            {"group": "Databases & Tooling", "skills": ["PostgreSQL", "Redis", "DuckDB", "Linux Kernel", "Docker", "Cargo", "GDB", "Perf"]}
        ],
        "projects": [
            ("raft-rs Distributed Consensus", "Pure-Rust implementation of Raft with pipelined leader append entries and dynamic cluster membership."),
            ("lsm-uring Storage Engine", "Embedded LSM-tree database engine utilizing io_uring submission queues for NVMe direct I/O.")
        ],
        "education": [{"degree": "M.Sc. Distributed Computing", "school": "KTH Royal Institute of Technology"}],
        "languages": [("Swedish", "Native"), ("English", "Fluent / C2"), ("German", "Professional")]
    }, os.path.join(output_dir, "elena_rust_distributed_storage_engineer.pdf"))

    # 3. Marcus Vance
    render_full_page_resume({
        "name": "Marcus Vance",
        "title": "Staff Machine Learning Infrastructure Engineer",
        "tagline": "Specialist in Large-Scale GPU Serving, CUDA Kernel Optimization & vLLM",
        "location": "San Francisco, CA",
        "phone": "+1 415 889 4210",
        "email": "marcus.vance@hyperscale.ai",
        "github": "github.com/mvance-ai",
        "profile": [
            "Staff ML infrastructure specialist with 9+ years architecting high-throughput GPU serving clusters, distributed model parallel training, and low-latency LLM inference pipelines. Deep expertise in CUDA kernel acceleration, vLLM continuous batching, FlashAttention, and high-dimensional vector search."
        ],
        "experience": [
            {
                "role": "Staff ML Infrastructure Engineer",
                "company": "HyperScale AI",
                "location": "San Francisco",
                "dates": "04/2022 - Present",
                "points": ["Architected GPU serving infrastructure for 70B+ parameter generative LLMs across 500+ H100 GPUs. Integrated vLLM PagedAttention and custom Triton CUDA kernels, cutting time-to-first-token by 55% while doubling sustained token throughput."],
                "stack": "PyTorch, CUDA, vLLM, TensorRT-LLM, Triton, Slurm, NCCL, RDMA"
            },
            {
                "role": "Senior ML Platform Engineer",
                "company": "NeuralStack",
                "location": "Palo Alto",
                "dates": "02/2018 - 03/2022",
                "points": ["Built real-time dense vector embedding search platform processing 40,000 queries per second over 500M document vectors using HNSW approximate nearest neighbors and Libsepal indexing."],
                "stack": "Python, C++, Vector DBs, HNSW, FAISS, PyTorch, Kubernetes"
            },
            {
                "role": "Machine Learning Engineer",
                "company": "VisionCore",
                "location": "San Jose",
                "dates": "07/2015 - 01/2018",
                "points": ["Trained computer vision convolutional models and optimized TensorRT inference pipelines on edge hardware for autonomous drones."],
                "stack": "C++, CUDA, OpenCV, Python, TensorFlow, ONNX"
            },
            {
                "role": "Research Assistant",
                "company": "Stanford AI Lab",
                "location": "Stanford",
                "dates": "09/2013 - 06/2015",
                "points": ["Co-authored papers on distributed stochastic gradient descent algorithms and asynchronous model parallel parameter servers."],
                "stack": "Python, C++, CUDA, MPI, OpenMP"
            }
        ],
        "skill_groups": [
            {"group": "Deep Learning & LLMs", "skills": ["PyTorch", "vLLM", "PagedAttention", "TensorRT-LLM", "Triton Server", "Transformers", "FlashAttention"]},
            {"group": "GPU Systems & Parallelism", "skills": ["CUDA", "C++", "Triton Lang", "NCCL", "Slurm", "Megatron-LM", "FSDP", "DeepSpeed ZeRO-3"]},
            {"group": "Vector & Retrieval Search", "skills": ["Dense Embeddings", "HNSW", "FAISS", "Libsepal", "ANN Search", "Quantization FP8/INT4"]},
            {"group": "Cloud & Infrastructure", "skills": ["Kubernetes", "Docker", "AWS EC2/EKS", "Prometheus", "Linux Kernel", "WandB"]}
        ],
        "projects": [
            ("paged-kernel-ext", "High-performance custom CUDA FlashDecoding kernel for long-context KV cache management."),
            ("vector-mesh", "Low-latency distributed vector indexing daemon in C++ handling billions of embedding vectors.")
        ],
        "education": [{"degree": "M.Sc. Computer Science (AI)", "school": "Stanford University"}],
        "languages": [("English", "Native"), ("French", "Working")]
    }, os.path.join(output_dir, "marcus_ml_nlp_infra_engineer.pdf"))

    # 4. Sophia Lindqvist
    render_full_page_resume({
        "name": "Sophia Lindqvist",
        "title": "Lead Product & Design Systems Designer",
        "tagline": "Specialist in Scalable Design Tokens, Component Architecture & WCAG 2.2 AAA",
        "location": "Copenhagen, Denmark",
        "phone": "+45 31 44 2099",
        "email": "sophia@nordicui.design",
        "github": "github.com/sophialindqvist",
        "profile": [
            "Lead product designer with 8+ years crafting intuitive enterprise SaaS interfaces and scalable multi-brand design systems. Expert in Figma variables, design token architecture, visual typography hierarchies, user research methodologies, and strict WCAG 2.2 AAA accessibility standards."
        ],
        "experience": [
            {
                "role": "Lead Design Systems Architect",
                "company": "NordicUI",
                "location": "Copenhagen",
                "dates": "05/2021 - Present",
                "points": ["Architected multi-brand enterprise design system in Figma adopted across 14 product teams. Governed design token pipelines connecting Figma variables directly to CSS and React component libraries."],
                "stack": "Figma, Design Tokens, Style Dictionary, Storybook, React UI"
            },
            {
                "role": "Senior Product Designer",
                "company": "CreativeFlow SaaS",
                "location": "Copenhagen",
                "dates": "08/2017 - 04/2021",
                "points": ["Redesigned complex analytics dashboards and onboarding flows based on comprehensive user research, increasing product retention by 28% and reducing support ticket volume."],
                "stack": "Figma, User Research, Usability Testing, Wireframing, Prototypes"
            },
            {
                "role": "UI/UX Designer",
                "company": "Studio Bold",
                "location": "Aarhus",
                "dates": "02/2015 - 07/2017",
                "points": ["Crafted digital brand identities, responsive web layouts, and interactive design prototypes for international luxury retail clients."],
                "stack": "Figma, Adobe Creative Suite, Typography, CSS3, InVision"
            },
            {
                "role": "Junior Graphic Designer",
                "company": "Danish Design Agency",
                "location": "Aarhus",
                "dates": "06/2013 - 01/2015",
                "points": ["Designed print publications, corporate brochures, and digital iconography systems following Scandinavian minimalist aesthetic principles."],
                "stack": "Illustrator, InDesign, Photoshop, Color Theory"
            }
        ],
        "skill_groups": [
            {"group": "Design Systems & UI", "skills": ["Figma Variables", "Design Tokens", "Component Architecture", "Atomic Design", "Theming", "Style Dictionary"]},
            {"group": "User Experience Design", "skills": ["User Journey Mapping", "Usability Testing", "Wireframing", "Interactive Prototyping", "User Interviews"]},
            {"group": "Visual & Accessibility", "skills": ["Typography Hierarchy", "WCAG 2.2 AAA", "Color Theory", "Micro-Interactions", "Grid Systems"]},
            {"group": "Frontend Collaboration", "skills": ["HTML5/CSS3", "Storybook", "Git", "React Component Specs", "Responsive Design"]}
        ],
        "projects": [
            ("NordicUI Enterprise Design System", "Unified multi-brand design tokens and component specs powering 6 distinct SaaS web applications."),
            ("A11y Color Palette Engine", "Algorithmic color generator ensuring strict WCAG AAA contrast compliance across dynamic dark/light themes.")
        ],
        "education": [{"degree": "B.A. Interaction Design", "school": "Copenhagen Institute of Interaction Design"}],
        "languages": [("Danish", "Native"), ("English", "Fluent / C2"), ("French", "Intermediate")]
    }, os.path.join(output_dir, "sophia_lead_product_designer.pdf"))

    # 5. David Chen
    render_full_page_resume({
        "name": "David Chen",
        "title": "Principal Site Reliability Engineer & Cloud Platform Architect",
        "tagline": "Specialist in Multi-Region Kubernetes, eBPF Cilium Networking & Linux Tuning",
        "location": "Seattle, WA",
        "phone": "+1 206 743 9102",
        "email": "david.chen@fincloud.net",
        "github": "github.com/dchen-sre",
        "profile": [
            "Principal SRE with 10+ years engineering mission-critical cloud platforms, high-availability multi-region Kubernetes clusters, and automated chaos engineering frameworks. Specializes in low-level Linux performance tuning, eBPF network observability, and zero-downtime infrastructure automation."
        ],
        "experience": [
            {
                "role": "Principal SRE",
                "company": "FinCloud Payments",
                "location": "Seattle",
                "dates": "01/2020 - Present",
                "points": ["Architected automated multi-region Kubernetes failover handling $10B+ annual payment transactions with 99.999% availability. Tuned Linux kernel TCP/IP congestion control (BBR) and sysctl parameters."],
                "stack": "Kubernetes, Terraform, AWS, eBPF, Cilium, Linux Kernel, BBR"
            },
            {
                "role": "Senior Cloud Infrastructure Engineer",
                "company": "DataMesh",
                "location": "Seattle",
                "dates": "03/2016 - 12/2019",
                "points": ["Spearheaded migration to Cilium eBPF networking, cutting cross-AZ latency by 35%. Deployed Prometheus, Grafana, and Thanos observability platform collecting 15M metrics/sec."],
                "stack": "Docker, Kubernetes, Prometheus, Grafana, Thanos, Terraform"
            },
            {
                "role": "Linux Systems Administrator",
                "company": "Pacific Hosting",
                "location": "Bellevue",
                "dates": "06/2013 - 02/2016",
                "points": ["Automated configuration management and high-availability PostgreSQL and Nginx clustering across 500+ physical Linux servers."],
                "stack": "Linux (Debian/RHEL), Bash, Python, Nginx, PostgreSQL"
            },
            {
                "role": "NOC Engineer",
                "company": "Northwest Telco",
                "location": "Seattle",
                "dates": "08/2011 - 05/2013",
                "points": ["Monitored nationwide backbone fiber routing, resolved BGP route flap anomalies, and managed 24/7 incident response."],
                "stack": "BGP, OSPF, Cisco IOS, Wireshark, SNMP, Nagios"
            }
        ],
        "skill_groups": [
            {"group": "Cloud & Orchestration", "skills": ["Kubernetes", "Docker", "Terraform", "AWS", "Google Cloud", "ArgoCD", "Helm", "Kustomize"]},
            {"group": "Observability & Networking", "skills": ["eBPF", "Cilium CNI", "Prometheus", "Grafana", "Thanos", "OpenTelemetry", "Service Mesh"]},
            {"group": "Systems & Reliability", "skills": ["Kernel sysctl tuning", "BBR", "Chaos Engineering", "Incident Response", "SLI/SLO"]},
            {"group": "Scripting & Tooling", "skills": ["Go", "Python", "Bash", "Linux Internals", "GitOps", "GitHub Actions"]}
        ],
        "projects": [
            ("k8s-chaos-operator", "Custom Kubernetes CRD operator injecting network partition latency via Linux tc/netem."),
            ("ebpf-flow-tracer", "Zero-overhead TCP connection lifecycle and packet drop analyzer written in C and Go.")
        ],
        "education": [{"degree": "B.Sc. Computer Engineering", "school": "University of Washington"}],
        "languages": [("English", "Native"), ("Mandarin", "Fluent")]
    }, os.path.join(output_dir, "david_devops_site_reliability_engineer.pdf"))

    # 6. Clara Mendes
    render_full_page_resume({
        "name": "Clara Mendes",
        "title": "Embedded Systems & IoT Firmware Engineer",
        "tagline": "Specialist in Bare-Metal C, FreeRTOS, ARM Cortex-M & Ultra-Low-Power BLE",
        "location": "Porto, Portugal",
        "phone": "+351 912 405 889",
        "email": "clara.mendes@sensorlogic.io",
        "github": "github.com/cmendes-embedded",
        "profile": [
            "Embedded firmware engineer with 6+ years experience in bare-metal C, real-time operating systems (FreeRTOS), and ultra-low-power wireless sensor networks. Specializes in hardware bring-up, microcontroller peripheral drivers, and low-energy communication protocols for industrial IoT."
        ],
        "experience": [
            {
                "role": "Senior Embedded Engineer",
                "company": "SensorLogic IoT",
                "location": "Porto",
                "dates": "02/2021 - Present",
                "points": ["Developed ultra-low-power FreeRTOS firmware for wireless industrial condition monitoring sensors on ARM Cortex-M4. Achieved 5-year continuous operation on a single coin-cell battery using deep sleep modes."],
                "stack": "Bare-Metal C, FreeRTOS, ARM Cortex-M, STM32, BLE 5.2, Low-Power"
            },
            {
                "role": "IoT Firmware Developer",
                "company": "SmartGrid Devices",
                "location": "Aveiro",
                "dates": "09/2018 - 01/2021",
                "points": ["Implemented secure boot, encrypted over-the-air (OTA) firmware update protocols, and robust CAN-to-BLE gateway for EV charging stations on ESP32."],
                "stack": "C, C++, ESP32, FreeRTOS, CAN Bus, SPI, UART, OTA Updates"
            },
            {
                "role": "Junior Hardware / Firmware Engineer",
                "company": "InnoWave Embedded",
                "location": "Porto",
                "dates": "07/2017 - 08/2018",
                "points": ["Designed PCB schematics, performed hardware bring-up using oscilloscopes and logic analyzers, and wrote peripheral drivers for I2C and ADC sensors."],
                "stack": "C, KiCAD, Oscilloscopes, Logic Analyzers, I2C, SPI"
            },
            {
                "role": "Embedded Lab Assistant",
                "company": "INESC TEC",
                "location": "Porto",
                "dates": "09/2015 - 06/2017",
                "points": ["Programmed digital signal processing filters on TI MSP430 microcontrollers for acoustic wildlife monitoring sensor buoys."],
                "stack": "Assembly, C, MSP430, DSP, Soldering, Hardware Testing"
            }
        ],
        "skill_groups": [
            {"group": "Firmware & RTOS", "skills": ["Bare-Metal C", "Embedded C++", "FreeRTOS", "Zephyr RTOS", "Interrupts (NVIC)", "DMA", "RTOS Mutexes"]},
            {"group": "Hardware & Microcontrollers", "skills": ["ARM Cortex-M", "STM32", "ESP32", "nRF52", "I2C/SPI/UART", "CAN Bus", "ADC/DAC"]},
            {"group": "Wireless & Power Optimization", "skills": ["BLE 5.2", "Low-Power Modes", "Energy Harvesting", "Zigbee", "LoRaWAN", "OTA Bootloaders"]},
            {"group": "Lab Equipment & Tooling", "skills": ["Oscilloscopes", "Logic Analyzers", "JTAG/SWD", "J-Link", "KiCad", "GCC ARM", "Makefiles"]}
        ],
        "projects": [
            ("ble-sensor-beacon", "Sub-10uA advertising BLE sensor firmware on nRF52840 with dynamic transmission power scaling."),
            ("can-gateway-freertos", "Dual-CAN to Ethernet industrial gateway with FreeRTOS zero-copy packet queuing.")
        ],
        "education": [{"degree": "B.Sc. Electrical & Computers Engineering", "school": "University of Porto (FEUP)"}],
        "languages": [("Portuguese", "Native"), ("English", "Fluent / C2"), ("Spanish", "Working")]
    }, os.path.join(output_dir, "clara_embedded_firmware_iot_engineer.pdf"))

    print(f"All 6 full 1-page test resumes successfully built and saved in: {output_dir}")

if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "test_resumes")
    build_all(out_dir)
