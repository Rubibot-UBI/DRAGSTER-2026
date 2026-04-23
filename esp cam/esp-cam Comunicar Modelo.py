import socket
import tkinter as tk
from tkinter import messagebox
from PIL import Image, ImageTk
import io

# CONFIGURATION
UDP_PORT = 5005        
TCP_PORT = 80          

class InferenceApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32-CAM AI Inference")
        self.root.geometry("550x650") 
        
        self.tcp_sock = None
        self.sock_file = None
        self.esp_ip = None
        
        # Build UI
        self.status_label = tk.Label(root, text="Starting network discovery...", font=("Arial", 12))
        self.status_label.pack(pady=10)

        self.img_panel = tk.Label(root, text="Waiting for connection...", bg="gray", width=60, height=25)
        self.img_panel.pack(pady=10)

        self.prediction_label = tk.Label(root, text="Prediction: N/A", font=("Arial", 16, "bold"), fg="blue")
        self.prediction_label.pack(pady=10)

        self.btn_frame = tk.Frame(root)
        self.btn_frame.pack(pady=10)

        self.btn_capture = tk.Button(self.btn_frame, text="Capture & Predict", command=self.capture_and_predict, state=tk.DISABLED, width=20, bg="green", fg="white", font=("Arial", 12, "bold"))
        self.btn_capture.pack()

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
            self.tcp_sock.settimeout(10.0) # Longer timeout for inference
            
            self.sock_file = self.tcp_sock.makefile('rb') 
            self.status_label.config(text=f"Connected to {self.esp_ip}! Ready.")
            self.btn_capture.config(state=tk.NORMAL)
            
        except Exception as e:
            messagebox.showerror("Connection Error", f"Failed to connect via TCP: {e}")
            self.setup_udp_listener()

    def force_reconnect(self):
        self.btn_capture.config(state=tk.DISABLED)
        if self.sock_file:
            self.sock_file.close()
        if self.tcp_sock:
            self.tcp_sock.close()
        self.setup_udp_listener()

    def read_until(self, delimiter):
        buf = bytearray()
        while True:
            c = self.sock_file.read(1)
            if not c:
                return bytes(buf)
            buf.extend(c)
            if buf.endswith(delimiter):
                return bytes(buf)

    def capture_and_predict(self):
        self.btn_capture.config(state=tk.DISABLED)
        self.status_label.config(text="Requesting inference from ESP32...")
        self.prediction_label.config(text="Prediction: Processing...", fg="orange")
        self.root.update()

        try:
            self.tcp_sock.sendall(b'C')  

            # Protocol: <START>length,prediction,image_bytes<END>
            raw_response = self.read_until(b'<START>')
            
            if not raw_response.endswith(b'<START>'):
                print("DEBUG: Stream desync. Reconnecting...")
                self.force_reconnect()
                return

            # Read image length
            length_str = self.read_until(b',')[:-1].decode('utf-8')
            length = int(length_str)
            
            # Read prediction class
            prediction_str = self.read_until(b',')[:-1].decode('utf-8')
            
            # Read image payload
            image_data = self.sock_file.read(length)
            
            # Read end tag
            end_tag = self.sock_file.read(5)
            if end_tag != b'<END>':
                print("DEBUG: Corrupt frame. Reconnecting...")
                self.force_reconnect()
                return
            
            self.display_results(image_data, prediction_str)
            
        except Exception as e:
            print(f"Error during network communication: {e}")
            self.force_reconnect()

    def display_results(self, image_data, prediction):
        try:
            # Render Image (L = 8-bit pixels, black and white)
            # Lê os bytes crus sabendo que a resolução original é 96x96
            image = Image.frombytes('L', (96, 96), image_data)
            image = image.resize((480, 480), Image.Resampling.LANCZOS)
            photo = ImageTk.PhotoImage(image)
            
            self.img_panel.config(image=photo, text="", width=480, height=480)
            self.img_panel.image = photo  
            
            # Update Labels
            self.status_label.config(text="Inference complete! Ready for next.")
            
            # Map the prediction string to your actual class names
            class_name = "Class 1" if prediction == "1" else "Class 0"
            color = "green" if prediction == "1" else "red"
            
            self.prediction_label.config(text=f"Prediction: {class_name}", fg=color)
            self.btn_capture.config(state=tk.NORMAL)
            
        except Exception as e:
            print(f"Image rendering error: {e}")
            self.status_label.config(text="Error rendering image. Try again.")
            self.btn_capture.config(state=tk.NORMAL)

# Start application
if __name__ == "__main__":
    root = tk.Tk()
    app = InferenceApp(root)
    
    def on_closing():
        if app.sock_file:
            app.sock_file.close()
        if app.tcp_sock:
            app.tcp_sock.close()
        root.destroy()
        
    root.protocol("WM_DELETE_WINDOW", on_closing)
    root.mainloop()