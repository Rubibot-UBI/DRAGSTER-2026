# Tirar fotos em Burst

import socket
import re
import os
import tkinter as tk
from tkinter import messagebox
from PIL import Image, ImageTk
import io

# CONFIGURATION
LABEL = "right"
SAVE_DIR = f"dataset/{LABEL}"
TARGET_IMAGES = 500  # Updated to capture 500 photos rapidly
UDP_PORT = 5005        
TCP_PORT = 80          

# Create directory if it doesn't exist
os.makedirs(SAVE_DIR, exist_ok=True)

def get_last_image_index(folder, label):
    # CHANGED: Look for .png files instead of .jpg
    pattern = re.compile(rf"{re.escape(label)}_(\d+)\.png")
    max_index = 0
    if not os.path.exists(folder):
        return 0
    for filename in os.listdir(folder):
        match = pattern.match(filename)
        if match:
            num = int(match.group(1))
            if num > max_index:
                max_index = num
    return max_index

class ImageCollectorApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Burst Dataset Collector")
        self.root.geometry("550x600") 
        
        self.start_index = get_last_image_index(SAVE_DIR, LABEL) + 1
        self.image_index = self.start_index
        self.target_index = self.start_index + TARGET_IMAGES - 1
        
        self.current_image_data = None
        self.tcp_sock = None
        self.sock_file = None
        self.esp_ip = None
        
        # Build UI
        self.status_label = tk.Label(root, text="Starting...", font=("Arial", 12))
        self.status_label.pack(pady=10)

        self.img_panel = tk.Label(root, text="Waiting for connection...", bg="gray")
        self.img_panel.pack(pady=10)

        # Start the network connection flow
        self.setup_udp_listener()

    def setup_udp_listener(self):
        self.status_label.config(text="Listening for ESP32 broadcast...")
        self.udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.udp_sock.bind(("", UDP_PORT))
        self.udp_sock.settimeout(0.5) 
        
        self.root.after(100, self.listen_for_broadcast)

    def listen_for_broadcast(self):
        try:
            data, addr = self.udp_sock.recvfrom(1024)
            if b"ESP32_CAM" in data:
                self.esp_ip = addr[0]
                self.udp_sock.close()
                self.connect_tcp()
                return
        except socket.timeout:
            pass 
        except Exception as e:
            print(f"UDP Error: {e}")
            
        self.root.after(100, self.listen_for_broadcast)

    def connect_tcp(self):
        self.status_label.config(text=f"Connecting to ESP32 at {self.esp_ip}...")
        self.root.update()
        try:
            self.tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_sock.connect((self.esp_ip, TCP_PORT))
            self.tcp_sock.settimeout(5.0)
            self.sock_file = self.tcp_sock.makefile('rb') 
            
            self.status_label.config(text="Connected! Starting Burst Capture...")
            self.root.after(200, self.capture_image)
            
        except Exception as e:
            messagebox.showerror("Connection Error", f"Failed to connect: {e}")
            self.setup_udp_listener()

    def force_reconnect(self):
        if self.sock_file: self.sock_file.close()
        if self.tcp_sock: self.tcp_sock.close()
        self.setup_udp_listener()

    def read_until(self, delimiter):
        buf = bytearray()
        while True:
            c = self.sock_file.read(1)
            if not c: return bytes(buf)
            buf.extend(c)
            if buf.endswith(delimiter): return bytes(buf)

    def capture_image(self):
        if self.image_index > self.target_index:
            self.status_label.config(text=f"Successfully captured {TARGET_IMAGES} images!")
            if self.tcp_sock: self.tcp_sock.close()
            return

        try:
            # Request frame
            self.tcp_sock.sendall(b'C')  
            raw_response = self.read_until(b'<START>')
            
            if not raw_response.endswith(b'<START>'):
                self.force_reconnect()
                return

            length_str = self.read_until(b',')[:-1].decode('utf-8')
            length = int(length_str)
            self.current_image_data = self.sock_file.read(length)
            
            end_tag = self.sock_file.read(5)
            if end_tag != b'<END>':
                self.force_reconnect()
                return
            
            self.process_and_save()
            
        except Exception as e:
            print(f"Network error: {e}")
            self.force_reconnect()

    def process_and_save(self):
        try:
            # CHANGED: Convert raw bytes to a Grayscale ('L') PIL Image of size 96x96
            image = Image.frombytes('L', (96, 96), self.current_image_data)

            # 1. Save to disk as PNG to keep it completely lossless without JPEG artifacts
            filename = os.path.join(SAVE_DIR, f"{LABEL}_{self.image_index}.png")
            image.save(filename)

            # 2. Update UI to act like a video feed
            display_image = image.resize((480, 480), Image.Resampling.LANCZOS)
            photo = ImageTk.PhotoImage(display_image)
            self.img_panel.config(image=photo, text="")
            self.img_panel.image = photo  
            
            progress = (self.image_index - self.start_index + 1)
            self.status_label.config(text=f"Burst Capturing: {progress} / {TARGET_IMAGES}")
            
            self.image_index += 1
            
            # 3. Immediately trigger next capture (1ms delay just to allow Tkinter to draw)
            self.root.after(1, self.capture_image)
            
        except Exception as e:
            print(f"Processing error: {e}")
            # If the frame was corrupt, just ask for the next one
            self.root.after(10, self.capture_image)

# Start application
if __name__ == "__main__":
    root = tk.Tk()
    app = ImageCollectorApp(root)
    
    def on_closing():
        if app.sock_file: app.sock_file.close()
        if app.tcp_sock: app.tcp_sock.close()
        root.destroy()
        
    root.protocol("WM_DELETE_WINDOW", on_closing)
    root.mainloop()