import csv
import matplotlib.pyplot as plt
import numpy as np
from read_csv import read_csv

# Set the style and font for the plots.
plt.style.use('seaborn-v0_8-white')
plt.rc('font',family='WenQuanYi Micro Hei', size=18)
width = 0.35

# Plot 1.

fig, ax1 = plt.subplots()
ax1.set_yscale('log')
ax1.set_xlabel('请求数量')
ax1.set_ylabel('运行时间 (秒)')

x, y = read_csv('plot/csv/efficiency/time.csv')
ax1.bar(x, y, label='运行时间', color='C2', width=width)

ax2 = ax1.twinx()
ax2.set_ylim(0, 75)
ax2.set_ylabel('状态空间直径')

x, y = read_csv('plot/csv/efficiency/diameter.csv')
ax2.plot(x, y, label='状态空间直径', color='C3', marker='o')

lines1, labels1 = ax1.get_legend_handles_labels()
lines2, labels2 = ax2.get_legend_handles_labels()
ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left', fontsize=16)
plt.tight_layout()
plt.savefig('plot/figures/efficiency-1.svg', transparent=True)

plt.clf()

# Plot 2.

plt.yscale('log')
plt.xlabel('请求数量')
plt.ylabel('状态空间大小')

x, y = read_csv('plot/csv/efficiency/distinct-state.csv')
plt.bar(np.array(x) - width / 2, y, label='不同状态数', width=width, color='C0')

x, y = read_csv('plot/csv/efficiency/found-state.csv')
plt.bar(np.array(x) + width / 2, y, label='访问状态数', width=width, color='C1')

plt.legend(loc='upper left', fontsize=16)
plt.tight_layout()
plt.savefig('plot/figures/efficiency-2.svg', transparent=True)
plt.clf()
