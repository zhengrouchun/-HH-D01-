# -*- coding: utf-8 -*-
"""Generate Problem 3 paper figures from the existing optimizer CSV outputs."""

from datetime import datetime, time
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


BASE_DIR = Path(__file__).resolve().parent
ATTACHMENT_2 = BASE_DIR / "附件2.xlsx"
ATTACHMENT_3 = BASE_DIR / "附件3.xlsx"
DETAIL_FILE = BASE_DIR / "problem3_detail.csv"
SUMMARY_FILE = BASE_DIR / "problem3_scenario_summary.csv"
OUTPUT_DIR = BASE_DIR / "paper_figures"

# Use one of the four dates required by the result template.
TYPICAL_DATE = pd.Timestamp("2025-06-21")


def set_plot_style():
    plt.rcParams["font.sans-serif"] = [
        "Microsoft YaHei",
        "SimHei",
        "Arial Unicode MS",
        "DejaVu Sans",
    ]
    plt.rcParams["axes.unicode_minus"] = False
    plt.rcParams["font.size"] = 10
    plt.rcParams["axes.titlesize"] = 13
    plt.rcParams["axes.labelsize"] = 11
    plt.rcParams["legend.fontsize"] = 9


def parse_clock(value):
    if isinstance(value, datetime):
        return value.hour * 60 + value.minute
    if isinstance(value, time):
        return value.hour * 60 + value.minute

    text = str(value).strip().replace(" ", "")
    if not text or text.lower() == "nan":
        raise ValueError(f"无法解析时间: {value}")

    next_day = "+1" in text
    text = text.replace("+1", "")
    hour, minute = [int(x) for x in text.split(":")[:2]]
    result = hour * 60 + minute
    return result + 1440 if next_day else result


def read_actual_pv():
    raw = pd.read_excel(
        ATTACHMENT_2,
        sheet_name="光伏发电实际功率",
        header=0,
    )
    date_column = raw.columns[0]
    rows = []

    for _, row in raw.iterrows():
        if pd.isna(row[date_column]):
            continue

        date = pd.to_datetime(
            row[date_column],
            format="mixed",
            errors="coerce",
        )
        if pd.isna(date):
            continue
        date = date.normalize()

        for column in raw.columns[1:]:
            value = pd.to_numeric(row[column], errors="coerce")
            if pd.isna(value):
                continue

            timestamp = date + pd.Timedelta(
                minutes=parse_clock(column)
            )
            rows.append(
                {
                    "timestamp": timestamp,
                    "actual_pv_kw": float(value),
                }
            )

    result = pd.DataFrame(rows)
    if result.empty:
        raise ValueError("附件2的光伏实际功率没有可用数据")
    return result.sort_values("timestamp").reset_index(drop=True)


def read_forecasts():
    raw = pd.read_excel(
        ATTACHMENT_3,
        sheet_name="Sheet1",
        header=0,
    )

    raw["日期"] = raw["日期"].replace("", np.nan).ffill()
    forecast_columns = [
        f"预报{i}小时"
        for i in range(1, 25)
    ]

    required = {"日期", "预报时刻", *forecast_columns}
    missing = required - set(raw.columns)
    if missing:
        raise ValueError(f"附件3缺少字段: {sorted(missing)}")

    records = []
    for _, row in raw.iterrows():
        if pd.isna(row["日期"]) or pd.isna(row["预报时刻"]):
            continue

        date = pd.to_datetime(
            row["日期"],
            format="mixed",
            errors="coerce",
        )
        if pd.isna(date):
            continue

        issue = date.normalize() + pd.Timedelta(
            minutes=parse_clock(row["预报时刻"])
        )
        values = pd.to_numeric(
            row[forecast_columns],
            errors="coerce",
        ).to_numpy(dtype=float)

        if len(values) != 24 or not np.isfinite(values).all():
            continue

        for hour, value in enumerate(values, start=1):
            records.append(
                {
                    "issue_timestamp": issue,
                    "target_timestamp": issue
                    + pd.Timedelta(hours=hour),
                    "forecast_pv_kw": float(value),
                    "release": issue.strftime("%H:%M"),
                }
            )

    result = pd.DataFrame(records)
    if result.empty:
        raise ValueError("附件3没有可用的光伏预测数据")
    return result.sort_values("target_timestamp").reset_index(drop=True)


def read_detail():
    if not DETAIL_FILE.exists():
        raise FileNotFoundError(
            "没有找到 problem3_detail.csv。"
            "请先运行 problem3_optimizer.py。"
        )

    detail = pd.read_csv(DETAIL_FILE)
    required = {
        "date",
        "time",
        "load_kw",
        "actual_pv_kw",
        "plan_buy_kw",
        "adjusted_buy_kw",
        "emergency_buy_kw",
        "charge_kw",
        "discharge_kw",
        "soc_kwh",
    }
    missing = required - set(detail.columns)
    if missing:
        raise ValueError(
            f"problem3_detail.csv缺少字段: {sorted(missing)}"
        )

    detail["timestamp"] = pd.to_datetime(
        detail["date"].astype(str)
        + " "
        + detail["time"].astype(str),
        format="mixed",
        errors="coerce",
    )
    if detail["timestamp"].isna().any():
        raise ValueError("problem3_detail.csv中存在无法解析的日期或时间")

    detail["date_only"] = detail["timestamp"].dt.normalize()
    detail["hour"] = (
        detail["timestamp"] - detail["date_only"]
    ).dt.total_seconds() / 3600

    if "soc_end_kwh" not in detail.columns:
        detail["soc_end_kwh"] = detail["soc_kwh"]

    return detail.sort_values("timestamp").reset_index(drop=True)


def read_summary():
    if not SUMMARY_FILE.exists():
        raise FileNotFoundError(
            "没有找到 problem3_scenario_summary.csv。"
            "请先运行 problem3_optimizer.py。"
        )

    summary = pd.read_csv(SUMMARY_FILE)
    required = {
        "scenario",
        "total_cost_yuan",
        "emergency_energy_kwh",
        "emergency_cost_yuan",
    }
    missing = required - set(summary.columns)
    if missing:
        raise ValueError(
            f"problem3_scenario_summary.csv缺少字段: {sorted(missing)}"
        )
    return summary


def get_detail_day(detail, date):
    date = pd.Timestamp(date).normalize()
    result = detail[detail["date_only"] == date].copy()
    if len(result) != 144:
        raise ValueError(
            f"{date:%Y-%m-%d}的结果应有144个时段，"
            f"实际找到{len(result)}个"
        )
    return result.sort_values("timestamp").reset_index(drop=True)


def add_update_lines(ax):
    for hour in (0, 6, 12, 18):
        ax.axvline(
            hour,
            color="#777777",
            linestyle="--",
            linewidth=0.8,
            alpha=0.7,
        )
        if hour:
            ax.text(
                hour + 0.1,
                0.97,
                f"{hour}:00更新",
                transform=ax.get_xaxis_transform(),
                fontsize=8,
                color="#555555",
                va="top",
            )


def plot_forecast_updates(actual, forecasts):
    start = TYPICAL_DATE.normalize()
    end = start + pd.Timedelta(days=1)

    actual_day = actual[
        (actual["timestamp"] >= start)
        & (actual["timestamp"] <= end)
    ]
    forecast_day = forecasts[
        (forecasts["target_timestamp"] >= start)
        & (forecasts["target_timestamp"] <= end)
        & (
            forecasts["issue_timestamp"].dt.normalize()
            == start
        )
    ]

    fig, ax = plt.subplots(figsize=(10.5, 5.2))
    actual_x = (
        actual_day["timestamp"] - start
    ).dt.total_seconds() / 3600
    ax.plot(
        actual_x,
        actual_day["actual_pv_kw"],
        color="#111111",
        linewidth=2.3,
        label="实际光伏功率",
    )

    colors = {
        "00:00": "#2f5597",
        "06:00": "#70ad47",
        "12:00": "#ed7d31",
        "18:00": "#a64d79",
    }
    labels = {
        "00:00": "00:00预测",
        "06:00": "06:00预测",
        "12:00": "12:00预测",
        "18:00": "18:00预测",
    }

    for release, group in forecast_day.groupby("release"):
        x = (
            group["target_timestamp"] - start
        ).dt.total_seconds() / 3600
        ax.plot(
            x,
            group["forecast_pv_kw"],
            color=colors.get(release, "#888888"),
            linewidth=1.6,
            label=labels.get(release, f"{release}预测"),
        )

    add_update_lines(ax)
    ax.set(
        title=f"多时刻光伏预测与实际功率对比（{start:%Y-%m-%d}）",
        xlabel="时间 / h",
        ylabel="功率 / kW",
        xlim=(0, 24),
    )
    ax.set_xticks(np.arange(0, 25, 2))
    ax.grid(axis="y", linestyle=":", alpha=0.45)
    ax.legend(ncol=3, loc="upper left")
    fig.tight_layout()
    path = OUTPUT_DIR / "图3-1_多时刻光伏预测与实际功率对比.png"
    fig.savefig(path, dpi=300, bbox_inches="tight")
    plt.close(fig)
    return path


def plot_plan_adjusted(detail_day):
    fig, ax = plt.subplots(figsize=(10.5, 5.0))
    ax.plot(
        detail_day["hour"],
        detail_day["plan_buy_kw"],
        color="#2f5597",
        linewidth=1.8,
        label="计划购电功率",
    )
    ax.plot(
        detail_day["hour"],
        detail_day["adjusted_buy_kw"],
        color="#ed7d31",
        linewidth=1.8,
        label="调整购电功率",
    )
    add_update_lines(ax)
    ax.set(
        title=f"计划购电与调整购电对比（{TYPICAL_DATE:%Y-%m-%d}）",
        xlabel="时间 / h",
        ylabel="功率 / kW",
        xlim=(0, 24),
    )
    ax.set_xticks(np.arange(0, 25, 2))
    ax.grid(axis="y", linestyle=":", alpha=0.45)
    ax.legend(loc="upper right")
    fig.tight_layout()
    path = OUTPUT_DIR / "图3-2_计划购电与调整购电对比.png"
    fig.savefig(path, dpi=300, bbox_inches="tight")
    plt.close(fig)
    return path


def plot_balance_soc(detail_day):
    fig, ax1 = plt.subplots(figsize=(10.8, 5.5))
    x = detail_day["hour"].to_numpy()

    ax1.stackplot(
        x,
        detail_day["actual_pv_kw"].to_numpy(),
        detail_day["adjusted_buy_kw"].to_numpy(),
        detail_day["emergency_buy_kw"].to_numpy(),
        detail_day["discharge_kw"].to_numpy(),
        labels=[
            "实际光伏",
            "调整购电",
            "紧急购电",
            "储能放电",
        ],
        colors=[
            "#ffd966",
            "#9dc3e6",
            "#f4b183",
            "#70ad47",
        ],
        alpha=0.85,
    )

    ax1.plot(
        x,
        detail_day["load_kw"].to_numpy()
        + detail_day["charge_kw"].to_numpy(),
        color="#111111",
        linewidth=2.0,
        linestyle="--",
        label="总需求（负荷+充电）",
    )
    ax1.set(
        title=f"典型日微网能量平衡与储能状态（{TYPICAL_DATE:%Y-%m-%d}）",
        xlabel="时间 / h",
        ylabel="功率 / kW",
        xlim=(0, 24),
    )
    ax1.set_xticks(np.arange(0, 25, 2))
    ax1.grid(axis="y", linestyle=":", alpha=0.4)

    ax2 = ax1.twinx()
    soc_x = np.r_[
        detail_day["hour"].to_numpy(),
        24.0,
    ]
    soc_y = np.r_[
        detail_day["soc_kwh"].to_numpy(),
        detail_day["soc_end_kwh"].iloc[-1],
    ]
    ax2.plot(
        soc_x,
        soc_y,
        color="#7030a0",
        linewidth=2.2,
        label="储能电量",
    )
    ax2.axhline(
        1200,
        color="#c00000",
        linestyle=":",
        linewidth=1.1,
        label="储能上下限",
    )
    ax2.axhline(10800, color="#c00000", linestyle=":", linewidth=1.1)
    ax2.set_ylabel("储能电量 / kWh", color="#7030a0")
    ax2.tick_params(axis="y", labelcolor="#7030a0")
    ax2.set_ylim(0, 12000)

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax1.legend(
        lines1 + lines2,
        labels1 + labels2,
        ncol=3,
        loc="upper left",
    )
    fig.tight_layout()
    path = OUTPUT_DIR / "图3-3_微网能量平衡与储能状态.png"
    fig.savefig(path, dpi=300, bbox_inches="tight")
    plt.close(fig)
    return path


def plot_daily_soc(detail):
    grouped = detail.groupby("date_only", sort=True)
    daily = pd.DataFrame(
        {
            "date": list(grouped.groups.keys()),
            "start_soc": grouped["soc_kwh"].first().to_numpy(),
            "end_soc": grouped["soc_end_kwh"].last().to_numpy(),
        }
    )

    fig, ax = plt.subplots(figsize=(10.5, 4.8))
    ax.plot(
        daily["date"],
        daily["start_soc"],
        color="#2f5597",
        linewidth=1.2,
        label="每日00:00储能电量",
    )
    ax.plot(
        daily["date"],
        daily["end_soc"],
        color="#ed7d31",
        linewidth=1.2,
        label="每日24:00储能电量",
    )
    ax.axhline(1200, color="#c00000", linestyle=":", linewidth=1)
    ax.axhline(10800, color="#c00000", linestyle=":", linewidth=1)
    ax.set(
        title="全年储能电量的跨日变化",
        xlabel="日期",
        ylabel="储能电量 / kWh",
    )
    ax.grid(axis="y", linestyle=":", alpha=0.4)
    ax.legend()
    fig.tight_layout()
    path = OUTPUT_DIR / "图3-4_全年储能电量跨日变化.png"
    fig.savefig(path, dpi=300, bbox_inches="tight")
    plt.close(fig)
    return path


def plot_scenario_comparison(summary):
    x = np.arange(len(summary))
    labels = summary["scenario"].astype(str).tolist()
    width = 0.36

    fig, axes = plt.subplots(1, 2, figsize=(10.5, 4.7))
    axes[0].bar(
        x - width / 2,
        summary["total_cost_yuan"] / 10000,
        width,
        color="#2f5597",
        label="总购电费用",
    )
    axes[0].bar(
        x + width / 2,
        summary["emergency_cost_yuan"] / 10000,
        width,
        color="#ed7d31",
        label="紧急购电费用",
    )
    axes[0].set_ylabel("费用 / 万元")
    axes[0].set_title("费用对比")
    axes[0].set_xticks(x, labels)
    axes[0].legend(fontsize=8)
    axes[0].grid(axis="y", linestyle=":", alpha=0.4)

    axes[1].bar(
        x,
        summary["emergency_energy_kwh"],
        color="#70ad47",
        width=0.55,
    )
    axes[1].set_ylabel("紧急购电量 / kWh")
    axes[1].set_title("紧急购电量对比")
    axes[1].set_xticks(x, labels)
    axes[1].grid(axis="y", linestyle=":", alpha=0.4)

    fig.suptitle("不同预测更新方式的运行结果对比", fontsize=13)
    fig.tight_layout()
    path = OUTPUT_DIR / "图3-5_不同预测更新方式结果对比.png"
    fig.savefig(path, dpi=300, bbox_inches="tight")
    plt.close(fig)

    summary.to_csv(
        OUTPUT_DIR / "表3-1_不同预测更新方式结果对比.csv",
        index=False,
        encoding="utf-8-sig",
    )
    return path


def main():
    set_plot_style()
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    actual = read_actual_pv()
    forecasts = read_forecasts()
    detail = read_detail()
    summary = read_summary()
    detail_day = get_detail_day(detail, TYPICAL_DATE)

    paths = [
        plot_forecast_updates(actual, forecasts),
        plot_plan_adjusted(detail_day),
        plot_balance_soc(detail_day),
        plot_daily_soc(detail),
        plot_scenario_comparison(summary),
    ]

    print("已生成以下文件：")
    for path in paths:
        print(path)


if __name__ == "__main__":
    main()
