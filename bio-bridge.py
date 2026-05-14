import serial
import re
import time

# ==================== НАСТРОЙКИ ====================
PORT_ECG = 'COM4'      # ЭКГ-модуль
PORT_ROBOT = 'COM7'    # Робот
BAUD_RATE = 9600     # Должна совпадать в обеих прошивках

# Пороги джойстика
VX_LEFT = 700
VX_RIGHT = 300
VY_UP = 700
VY_DOWN = 200

# Дедбанд для HR (не шлём команду, если пульс изменился меньше чем на 1.5 уд/мин)
HR_DEADBAND = 1.5
# ==================================================

def main():
    print("🌉 Запуск моста ЭКГ → Робот...")
    try:
        ser_ecg = serial.Serial(PORT_ECG, BAUD_RATE, timeout=0.1)
        ser_robot = serial.Serial(PORT_ROBOT, BAUD_RATE, timeout=0.1)
        time.sleep(1.5)
        ser_ecg.reset_input_buffer()
        ser_robot.reset_input_buffer()
        
        print(f"✅ Подключено: ECG@{PORT_ECG}, Robot@{PORT_ROBOT}")
        
        last_cmd = None
        last_hr = None
        
        while True:
            if ser_ecg.in_waiting:
                line = ser_ecg.readline().decode('utf-8', errors='ignore').strip()
                if not line: continue
                
                # 1. Парсинг данных
                hr_m = re.search(r'HR:(\d+(?:\.\d+)?)', line)
                vx_m = re.search(r'Vx:(\d+(?:\.\d+)?)', line)
                vy_m = re.search(r'Vy:(\d+(?:\.\d+)?)', line)
                
                hr = float(hr_m.group(1)) if hr_m else None
                vx = float(vx_m.group(1)) if vx_m else None
                vy = float(vy_m.group(1)) if vy_m else None

                print(f"hr:{hr} vx:{vx} vy:{vy}")
                
                if vx is None or vy is None: continue
                
                # 2. Отправка частоты (HR)
                if hr is not None and last_hr is not None and abs(hr - last_hr) > HR_DEADBAND:
                    ser_robot.write(f"H:{hr:.1f}\n".encode())
                    last_hr = hr
                    
                # 3. Логика движения
                new_cmd = None
                if vy > VY_UP: new_cmd = 'U'
                elif vy < VY_DOWN: new_cmd = 'S'
                elif vx > VX_LEFT: new_cmd = 'L'
                elif vx < VX_RIGHT: new_cmd = 'R'
                
                # Отправляем только при изменении состояния
                if new_cmd and new_cmd != last_cmd:
                    ser_robot.write(f"{new_cmd}\n".encode())
                    last_cmd = new_cmd
                    print(f"📤 CMD:{new_cmd} | HR:{hr:.1f} | Vx:{vx:.0f} Vy:{vy:.0f}")
                    
            time.sleep(0.02)  # ~50 Гц опрос, достаточно для плавности
            
    except KeyboardInterrupt:
        print("\n🛑 Остановка")
    except Exception as e:
        print(f"❌ Ошибка: {e}")
    finally:
        if 'ser_ecg' in locals() and ser_ecg.is_open: ser_ecg.close()
        if 'ser_robot' in locals() and ser_robot.is_open: ser_robot.close()

if __name__ == "__main__":
    main()