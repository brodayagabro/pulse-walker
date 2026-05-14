import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports
import re
import queue
import threading
import time
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

# ==================== НАСТРОЙКИ ====================
BAUD_ECG = 9600      # Скорость ЭКГ-модуля
BAUD_ROBOT = 9600      # Скорость робота
PLOT_POINTS = 400      # Окно графика (~1.2 сек при 333 Гц)
UPDATE_INTERVAL_MS = 40

# Пороги джойстика
VX_LEFT = 700
VX_RIGHT = 300
VY_UP = 700
VY_DOWN = 200

# Дедбанды (мин. изменение для отправки команды)
HR_DEADBAND = 1.5
RMSSD_DEADBAND = 2.0

# Диапазоны валидации
HR_VALID_RANGE = (40.0, 180.0)
RMSSD_VALID_RANGE = (0.0, 200.0)
# ===================================================

class ECGMonitorApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ECG Monitor | Bio-Hexapod Controller")
        self.root.geometry("900x700")
        self.root.minsize(750, 550)

        self.data_queue = queue.Queue()
        self.ser_ecg = None
        self.ser_robot = None
        self.stop_event = threading.Event()
        self.connected = False
        self.ecg_data = []

        # Состояние моста
        self.last_hr = None
        self.last_rmssd = None
        self.last_cmd = None
        
        # Выбранный источник биосигнала
        self.bio_source = tk.StringVar(value="HR")

        self._build_ui()
        self._update_gui()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self):
        # === Верхняя панель: Порты + Режим ===
        ctrl_frame = ttk.Frame(self.root, padding="8")
        ctrl_frame.pack(fill=tk.X)

        # COM-порты
        port_frame = ttk.Frame(ctrl_frame)
        port_frame.pack(side=tk.LEFT)
        
        ttk.Label(port_frame, text="ECG:", font=("Segoe UI", 9, "bold")).pack(side=tk.LEFT, padx=(0, 3))
        self.combo_ecg = ttk.Combobox(port_frame, width=12, state="readonly")
        self.combo_ecg.pack(side=tk.LEFT, padx=3)

        ttk.Label(port_frame, text="Robot:", font=("Segoe UI", 9, "bold")).pack(side=tk.LEFT, padx=(15, 3))
        self.combo_robot = ttk.Combobox(port_frame, width=12, state="readonly")
        self.combo_robot.pack(side=tk.LEFT, padx=3)

        ttk.Button(port_frame, text="↻", command=self._refresh_ports, width=3).pack(side=tk.LEFT, padx=10)

        # Переключатель био-режима
        mode_frame = ttk.LabelFrame(ctrl_frame, text=" Frequency source", padding="5")
        mode_frame.pack(side=tk.LEFT, padx=(30, 10))
        
        ttk.Radiobutton(mode_frame, text="HR (ЧСС)", variable=self.bio_source, 
                       value="HR", command=self._on_mode_change).pack(side=tk.LEFT, padx=5)
        ttk.Radiobutton(mode_frame, text="RMSSD (ВСР)", variable=self.bio_source, 
                       value="RMSSD", command=self._on_mode_change).pack(side=tk.LEFT, padx=5)

        # Кнопка подключения
        self.btn_connect = ttk.Button(ctrl_frame, text="🔌 Connect", command=self._toggle_connection, width=14)
        self.btn_connect.pack(side=tk.RIGHT, padx=10)

        self.status_lbl = ttk.Label(ctrl_frame, text="● Disconnected", foreground="gray", font=("Segoe UI", 9))
        self.status_lbl.pack(side=tk.RIGHT, padx=10)

        self._refresh_ports()

        # === График ЭКГ ===
        plot_frame = ttk.LabelFrame(self.root, text=" ECG signal (0-1023)", padding="5")
        plot_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        self.fig = Figure(figsize=(6, 4), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.line, = self.ax.plot([], [], color="#e74c3c", linewidth=1.8)
        
        self.ax.set_ylim(0, 1023)
        self.ax.set_xlim(0, PLOT_POINTS)
        self.ax.set_xlabel("Time")
        self.ax.set_ylabel("Signal")
        self.ax.grid(True, linestyle="--", alpha=0.5)
        self.ax.set_facecolor("#f8f9fa")

        self.canvas = FigureCanvasTkAgg(self.fig, master=plot_frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        # === Нижняя панель: Метрики ===
        metrics_frame = ttk.Frame(self.root, padding="12")
        metrics_frame.pack(fill=tk.X, padx=10, pady=5)

        # Большие метрики
        metrics_top = ttk.Frame(metrics_frame)
        metrics_top.pack(fill=tk.X)

        self.hr_label = ttk.Label(metrics_top, text="💓 HR: --.-", 
                                  font=("Segoe UI", 24, "bold"), foreground="#2c3e50")
        self.hr_label.pack(side=tk.LEFT, padx=20)

        self.rmssd_label = ttk.Label(metrics_top, text=" RMSSD: --.-", 
                                     font=("Segoe UI", 24, "bold"), foreground="#3498db")
        self.rmssd_label.pack(side=tk.LEFT, padx=20)

        # Статус-бар
        self.info_lbl = ttk.Label(metrics_frame, 
                                  text="Waiting... | Mode: HR | ECG:9600bod | Robot:9600bod", 
                                  font=("Segoe UI", 9), foreground="gray", anchor="w")
        self.info_lbl.pack(fill=tk.X, pady=(8, 0))

    def _on_mode_change(self):
        """Обработка переключения режима"""
        mode = self.bio_source.get()
        self.info_lbl.config(text=f"Mode: {mode} | Bridge Active" if self.connected else f"Mode: {mode}")
        # Сбрасываем последние значения при смене режима
        self.last_hr = None
        self.last_rmssd = None

    def _refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.combo_ecg["values"] = ports
        self.combo_robot["values"] = ports
        if ports:
            self.combo_ecg.set(ports[0])
            self.combo_robot.set(ports[1] if len(ports) > 1 else ports[0])

    def _toggle_connection(self):
        if self.connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port_ecg = self.combo_ecg.get()
        port_robot = self.combo_robot.get()
        if not port_ecg or not port_robot:
            messagebox.showerror("Error", "Select COM-ports for ecg and robot")
            return
        try:
            self.ser_ecg = serial.Serial(port_ecg, BAUD_ECG, timeout=1)
            self.ser_robot = serial.Serial(port_robot, BAUD_ROBOT, timeout=1)
            
            self.stop_event.clear()
            self.serial_thread = threading.Thread(target=self._serial_reader, daemon=True)
            self.serial_thread.start()
            
            self.connected = True
            self.btn_connect.config(text=" Disconnect")
            self.status_lbl.config(text="● Connected", foreground="#27ae60")
            self.info_lbl.config(text=f"✅ Connected | Mode: {self.bio_source.get()}")
        except serial.SerialException as e:
            messagebox.showerror("Serial Error", f"{e}")
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def _disconnect(self):
        self.stop_event.set()
        if self.ser_ecg and self.ser_ecg.is_open: self.ser_ecg.close()
        if self.ser_robot and self.ser_robot.is_open: self.ser_robot.close()
        self.ser_ecg = None
        self.ser_robot = None
        self.connected = False
        
        self.btn_connect.config(text="🔌 Connect")
        self.status_lbl.config(text="● Disconnected", foreground="gray")
        self.ecg_data.clear()
        self.line.set_data([], [])
        self.canvas.draw_idle()
        self.hr_label.config(text="💓 HR: --.-")
        self.rmssd_label.config(text="📊 RMSSD: --.-")
        self.info_lbl.config(text=f"Mode: {self.bio_source.get()}")

    def _serial_reader(self):
        """Фоновый поток: чтение, парсинг, мост на робота"""
        while not self.stop_event.is_set():
            if self.ser_ecg and self.ser_ecg.in_waiting:
                try:
                    line = self.ser_ecg.readline().decode("utf-8", errors="ignore").strip()
                    if not line: continue

                    # === Парсинг всех полей ===
                    sig_match = re.search(r"SIG:(\d+)", line)
                    hr_match = re.search(r"HR:(\d+(?:\.\d+)?)", line)
                    rmssd_match = re.search(r"RMSSD:(\d+(?:\.\d+)?)", line)
                    vx_match = re.search(r"Vx:(\d+(?:\.\d+)?)", line)
                    vy_match = re.search(r"Vy:(\d+(?:\.\d+)?)", line)
                    
                    sig_val = int(sig_match.group(1)) if sig_match else None
                    hr_val = float(hr_match.group(1)) if hr_match else None
                    rmssd_val = float(rmssd_match.group(1)) if rmssd_match else None
                    vx_val = float(vx_match.group(1)) if vx_match else None
                    vy_val = float(vy_match.group(1)) if vy_match else None

                    # Отправка в GUI
                    if sig_val is not None:
                        self.data_queue.put(("ecg", sig_val))
                    if hr_val is not None:
                        self.data_queue.put(("hr", hr_val))
                    if rmssd_val is not None:
                        self.data_queue.put(("rmssd", rmssd_val))

                    # === МОСТ НА РОБОТА ===
                    if vx_val is not None and vy_val is not None:
                        mode = self.bio_source.get()
                        
                        # 1. Отправка био-сигнала (только выбранный источник)
                        if mode == "HR" and hr_val is not None:
                            if self.last_hr is None or abs(hr_val - self.last_hr) > HR_DEADBAND:
                                if HR_VALID_RANGE[0] <= hr_val <= HR_VALID_RANGE[1]:
                                    self.ser_robot.write(f"H:{hr_val:.1f}\n".encode())
                                    self.last_hr = hr_val
                                    self.data_queue.put(("bio_sent", f"H:{hr_val:.1f}"))
                                    
                        elif mode == "RMSSD" and rmssd_val is not None:
                            if self.last_rmssd is None or abs(rmssd_val - self.last_rmssd) > RMSSD_DEADBAND:
                                if RMSSD_VALID_RANGE[0] <= rmssd_val <= RMSSD_VALID_RANGE[1]:
                                    self.ser_robot.write(f"R:{rmssd_val:.1f}\n".encode())
                                    self.last_rmssd = rmssd_val
                                    self.data_queue.put(("bio_sent", f"R:{rmssd_val:.1f}"))

                        # 2. Движение от джойстика
                        new_cmd = None
                        if vy_val > VY_UP: new_cmd = 'U'
                        elif vy_val < VY_DOWN: new_cmd = 'S'
                        elif vx_val > VX_LEFT: new_cmd = 'L'
                        elif vx_val < VX_RIGHT: new_cmd = 'R'
                        
                        if new_cmd and new_cmd != self.last_cmd:
                            self.ser_robot.write(f"{new_cmd}\n".encode())
                            self.last_cmd = new_cmd
                            self.data_queue.put(("robot_cmd", new_cmd))
                            
                except Exception as e:
                    print(f"[Serial] Ошибка: {e}")
            time.sleep(0.005)

    def _update_gui(self):
        """Безопасное обновление из главного потока"""
        while not self.data_queue.empty():
            msg_type, value = self.data_queue.get()
            if msg_type == "ecg":
                self.ecg_data.append(value)
                if len(self.ecg_data) > PLOT_POINTS:
                    self.ecg_data.pop(0)
                self.line.set_data(range(len(self.ecg_data)), self.ecg_data)
            elif msg_type == "hr":
                self.hr_label.config(text=f"💓 HR: {value:.1f}")
            elif msg_type == "rmssd":
                self.rmssd_label.config(text=f"📊 RMSSD: {value:.1f}")
            elif msg_type == "robot_cmd":
                self.info_lbl.config(text=f"📡 CMD:{value} | Mode:{self.bio_source.get()} | Bridge Active")
            elif msg_type == "bio_sent":
                # Краткое подтверждение отправки био-команды
                pass

        self.canvas.draw_idle()
        self.root.after(UPDATE_INTERVAL_MS, self._update_gui)

    def _on_close(self):
        self._disconnect()
        self.root.destroy()

if __name__ == "__main__":
    plt.rcParams["font.family"] = "sans-serif"
    plt.rcParams["axes.grid"] = True
    
    root = tk.Tk()
    try:
        style = ttk.Style()
        style.theme_use("clam")
    except tk.TclError:
        pass
        
    app = ECGMonitorApp(root)
    root.mainloop()