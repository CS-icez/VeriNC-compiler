import csv
import matplotlib.pyplot as plt
import numpy as np
from read_csv import read_csv

# Set the style and font for the plots.
plt.style.use('seaborn-v0_8-white')
plt.rc('font',family='WenQuanYi Micro Hei', size=18)
width = 0.35

# Plot 1.

plt.xlabel('请求数量')
plt.ylabel('运行时间 (秒)')

x, y = read_csv('plot/csv/symmetry/time.csv')
plt.bar(np.array(x[1]) - width / 2, y[1], label='关闭优化', color='C4', width=width)
plt.bar(np.array(x[0]) + width / 2, y[0], label='开启优化', color='C5', width=width)

plt.legend(loc='upper left', fontsize=16)
plt.tight_layout()
plt.savefig('plot/figures/symmetry-1.pdf')
plt.clf()

# Plot 2.

plt.xlabel('请求数量')
plt.ylabel('不同状态数')

x, y = read_csv('plot/csv/symmetry/distinct-state.csv')
plt.bar(np.array(x[1]) - width / 2, y[1], label='关闭优化', width=width, color='C6')
plt.bar(np.array(x[0]) + width / 2, y[0], label='开启优化', width=width, color='C5')

plt.legend(loc='upper left', fontsize=16)
plt.tight_layout()
plt.savefig('plot/figures/symmetry-2.pdf')
plt.clf()
