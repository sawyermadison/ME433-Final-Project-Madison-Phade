import serial
import matplotlib.pyplot as plt

ser = serial.Serial('COM4', 115200)
print('Opening port: ' + ser.name)

# send trigger
ser.write(b'b\n')

# read n data points
n_str = ser.read_until(b'\n')
n = int(n_str)
print('Receiving ' + str(n) + ' data points...')

ref = []
data = []
for _ in range(n):
    dat_str = ser.read_until(b'\n')
    dat = list(map(float, dat_str.split()))
    ref.append(dat[0])
    data.append(dat[1])

ser.close()

t = range(len(ref))
plt.plot(t, ref, 'r*-', label='desired')
plt.plot(t, data, 'b*-', label='actual')
plt.legend()
plt.ylabel('Current (A)')
plt.xlabel('Index')
plt.title('PI Controller Tuning')
plt.show()