from dateutil import tz
from datetime import datetime, timedelta
import pandas as pd
import requests
from pyowm.owm import OWM
import sys
import os
import numpy as np

file_path = 'C:/Users/User/Desktop/UNI/MAGISTRALE/IoT/PROGETTO/conf.py'
directory_path = os.path.dirname(file_path)
sys.path.append(directory_path)
import conf

def displayDF(DF):
    df = DF.copy()
    destination_tz = tz.gettz('Etc/GMT-2')
    df['time'] = pd.to_datetime(df['_time']).dt.tz_convert(destination_tz)
    df['time'] = df['time'].dt.floor('S')
    df['time'] = df['time'].dt.tz_localize(None)
    df['date'] = df['time'].dt.date
    df['time_only'] = df['time'].dt.time
    df = df[['date', 'time_only', 'Sensor ID', 'moisture']]
    return df

def DF_for_Prophet(DF, last_irrigation):
    df = DF.copy()
    destination_tz = tz.gettz('Etc/GMT-2')
    df['time'] = pd.to_datetime(df['_time']).dt.tz_convert(destination_tz)
    df['time'] = df['time'].dt.floor('S')
    df['time'] = df['time'].dt.tz_localize(None)
    df = df[['time', 'moisture']]
    df = df.rename(columns={'time': 'ds'})
    df = df.rename(columns={'moisture': 'y'})
    df_filtered = df.loc[df['ds'] >= last_irrigation]
    return df_filtered

def rain_forecast(days):
    response = requests.get(conf.owmURL).json() 
    rain_data = []
    for entry in response['list']:
        timestamp = entry['dt_txt']  
        if 'rain' in entry and '3h' in entry['rain']:
            rain_amount = entry['rain']['3h']
        else:
            continue 

        precipitation_probability = entry.get('pop', 0)  
        rain_data.append((timestamp, rain_amount, precipitation_probability))
 
    df = pd.DataFrame(rain_data, columns=['time', 'rain', 'prob']) 
    df['time'] = pd.to_datetime(df['time']) 
    data_di_riferimento = datetime.now().date() + timedelta(days=days) 
    df_filtrato = df[df['time'].dt.date <= data_di_riferimento] 
    return df

def moving_average(df, window):
    data = df['y']
    coeff = np.ones(window)
    moving_averages = np.convolve(data, coeff/np.sum(coeff), mode='valid')
    aligned_moving_averages = np.concatenate((np.zeros(window - 1), moving_averages))
    for i in range(window):
        aligned_moving_averages[i] = data[i]

    DF = df.copy()
    DF['y'] = aligned_moving_averages
    return DF

def clean_df(df, std_th):
    dg = df.copy()
    window_size = '6H' 
    remove_index = []
    dg.set_index('ds', inplace=True)
    rolling_stats = dg['y'].rolling(window=window_size)
    mean_series = rolling_stats.mean()
    std_series = rolling_stats.std()

    for time, value in dg['y'].items():
        mean = mean_series[time]
        std = std_series[time]
        
        if std == 0: 
            continue
    
        threshold = std_th * std
        if abs(value - mean) > threshold:
            remove_index.append(time)
    dg_processed = dg.drop(remove_index)
    dg.reset_index(inplace=True)
    dg_processed.reset_index(inplace=True)
    return dg_processed