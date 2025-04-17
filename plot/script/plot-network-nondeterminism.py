import csv
import matplotlib.pyplot as plt
import numpy as np
from read_csv import read_csv

# Set the style and font for the plots.
plt.style.use('seaborn-v0_8-white')
plt.rc('font',family='WenQuanYi Micro Hei', size=18)

cnt = 1

def draw_bar_and_plot(file1, file2, xlabel):
    fig, ax1 = plt.subplots()
    # ax1.set_yscale('log')
    ax1.set_xlabel(xlabel)
    ax1.set_ylabel('运行时间 (秒)')

    x, y = read_csv(file1)
    ax1.bar(x, y, label='运行时间', color='C9', width=0.35)

    ax2 = ax1.twinx()
    # ax2.set_ylim(0, 75)
    ax2.set_ylabel('访问状态数')

    x, y = read_csv(file2)
    ax2.plot(x, y, label='访问状态数', color='C3', marker='o')

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left', fontsize=16)
    plt.tight_layout()

    global cnt
    plt.savefig('plot/figures/network-nondeterminism-' + str(cnt) + '.pdf')
    cnt += 1
    plt.clf()

draw_bar_and_plot('plot/csv/network-nondeterminism/loss-time.csv', 'plot/csv/network-nondeterminism/loss-found-state.csv', '最大丢失报文数量')
draw_bar_and_plot('plot/csv/network-nondeterminism/ooo-time.csv', 'plot/csv/network-nondeterminism/ooo-found-state.csv', '最大乱序报文数量')
draw_bar_and_plot('plot/csv/network-nondeterminism/dup-time.csv', 'plot/csv/network-nondeterminism/dup-found-state.csv', '最大重复报文数量')
