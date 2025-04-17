import matplotlib.pyplot as plt
import numpy as np

plt.style.use('seaborn-v0_8-white')
plt.rc('font',family='WenQuanYi Micro Hei', size=18)
width = 0.2
y = [1, 2, 3, 4]

plt.xlabel('百分比')
plt.xlim(0, 10)
plt.yticks(y, ['不同状态数', '访问状态数', '直径', '运行时间 (秒)'])

plt.barh(np.array(y) - width, [1, 1, 1, 1], label='无检查 (基准)', color='C0', height=width)
plt.barh(np.array(y), [1, 1, 1, 1.05], label='检查计算正确性', color='C1', height=width)
plt.barh(np.array(y) + width, [1, 1, 1, 7.33], label='检查可终止性', color='C2', height=width)

plt.legend(loc='lower right', fontsize=16)
plt.tight_layout()
plt.savefig('plot/figures/property.pdf')
plt.clf()
