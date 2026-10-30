#!/usr/bin/env python3
import os
import sys

def create_pdf(path, title, lines):
    content = "BT /F1 12 Tf 50 750 Td 16 TL\n"
    content += f"({title}) Tj T* T*\n"
    for line in lines:
        escaped = line.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
        content += f"({escaped}) Tj T*\n"
    content += "ET\n"
    
    stream_bytes = content.encode("latin1")
    stream_len = len(stream_bytes)
    
    pdf = bytearray()
    pdf.extend(b"%PDF-1.4\n")
    
    off1 = len(pdf)
    pdf.extend(b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n")
    
    off2 = len(pdf)
    pdf.extend(b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n")
    
    off3 = len(pdf)
    pdf.extend(b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n")
    
    off4 = len(pdf)
    pdf.extend(f"4 0 obj\n<< /Length {stream_len} >>\nstream\n".encode("latin1"))
    pdf.extend(stream_bytes)
    pdf.extend(b"\nendstream\nendobj\n")
    
    off5 = len(pdf)
    pdf.extend(b"5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n")
    
    xref_off = len(pdf)
    pdf.extend(b"xref\n0 6\n0000000000 65535 f \n")
    for off in [off1, off2, off3, off4, off5]:
        pdf.extend(f"{off:010d} 00000 n \n".encode("latin1"))
        
    pdf.extend(f"trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n{xref_off}\n%%EOF\n".encode("latin1"))
    
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(pdf)

if __name__ == "__main__":
    out_dir = os.path.dirname(os.path.abspath(__file__))

    create_pdf(os.path.join(out_dir, "alice_systems_engineer.pdf"),
               "Alice Smith - Senior C Systems Engineer", [
        "Summary: 10 years experience developing high-performance Linux network daemons in pure C.",
        "Core Skills: C, POSIX, Linux, TCP/IP sockets, memory management, multi-threading, Axil, Corm.",
        "Work Experience: Lead Systems Engineer at TechCorp building high-throughput microservices in C.",
        "Education: Bachelor of Science in Computer Science."
    ])

    create_pdf(os.path.join(out_dir, "bob_data_scientist.pdf"),
               "Bob Jones - Machine Learning & Data Scientist", [
        "Summary: Expert in training deep learning neural networks and natural language processing models.",
        "Core Skills: Python, PyTorch, TensorFlow, Pandas, LLMs, Transformer architectures, vector databases.",
        "Work Experience: Senior AI Researcher training large embedding models and generative pipelines.",
        "Education: Master of Science in Artificial Intelligence."
    ])

    create_pdf(os.path.join(out_dir, "carol_graphic_designer.pdf"),
               "Carol White - Creative UI/UX and Graphic Designer", [
        "Summary: Creative designer with 7 years crafting intuitive user interfaces and brand identities.",
        "Core Skills: Figma, Adobe Illustrator, Photoshop, typography, branding, wireframing, color theory.",
        "Work Experience: Lead Product Designer designing design systems and modern web interfaces.",
        "Education: Bachelor of Fine Arts in Graphic Design."
    ])

    print("Sample test PDFs successfully generated in", out_dir)
