"""Read Windows LAN addresses without changing adapters or firewall settings."""
import argparse
import ipaddress
import json
import os
import socket
import subprocess

def device_host(host):
    address=ipaddress.IPv4Address(host)
    if (address.is_loopback or address.is_unspecified or address.is_multicast or
        address.is_link_local or address==ipaddress.IPv4Address('255.255.255.255')):
        raise ValueError('Use the laptop LAN IPv4 address; not 127.0.0.1, 0.0.0.0 or a link-local address')
    return str(address)

def classify_adapter(name,description):
    value=(name+' '+description).lower()
    return any(word in value for word in ('vmware','virtual','vpn','vethernet','hyper-v','loopback','wireguard','docker','tap-','tun-'))

def usable_addresses(records):
    result=[]
    for record in records:
        try: host=device_host(record['ip'])
        except (KeyError,ValueError): continue
        if record.get('up') is False: continue
        item=dict(record,ip=host)
        item['virtual']=classify_adapter(item.get('name',''),item.get('description',''))
        result.append(item)
    return result

def lan_addresses():
    if os.name=='nt':
        # .NET enumeration avoids WMI/Get-NetIPConfiguration permission requirements.
        script=r'''
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$rows = @(
foreach ($adapter in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
    if ($adapter.OperationalStatus -ne 'Up') { continue }
    $props = $adapter.GetIPProperties()
    $gateway = @($props.GatewayAddresses | Where-Object {
        $_.Address.AddressFamily -eq 'InterNetwork' -and $_.Address.ToString() -ne '0.0.0.0'
    }).Count -gt 0
    foreach ($entry in $props.UnicastAddresses) {
        if ($entry.Address.AddressFamily -eq 'InterNetwork') {
            [PSCustomObject]@{name=$adapter.Name;description=$adapter.Description;
                ip=$entry.Address.ToString();gateway=$gateway;up=$true}
        }
    }
})
ConvertTo-Json -InputObject @($rows) -Compress
'''
        try:
            result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',script],
                                  capture_output=True,encoding='utf8',timeout=10,check=True)
            records=json.loads(result.stdout.lstrip('\ufeff'))
            return usable_addresses(records)
        except (OSError,ValueError,subprocess.SubprocessError): pass
    # Fallback deliberately has no automatic choice without an adapter identity.
    hosts=sorted({entry[4][0] for entry in socket.getaddrinfo(socket.gethostname(),None,socket.AF_INET)})
    return usable_addresses([dict(name='manual selection',description='',ip=host,gateway=False) for host in hosts])

def suggested_host(records):
    physical=[r for r in records if not r['virtual'] and r.get('gateway')]
    return physical[0]['ip'] if len(physical)==1 else None

def current_ssid():
    if os.name!='nt': return None
    try:
        raw=subprocess.check_output(['netsh','wlan','show','interfaces'],timeout=5)
        text=raw.decode('mbcs',errors='replace')
        values=[]
        for line in text.splitlines():
            name,sep,value=line.partition(':')
            if sep and name.strip()=='SSID' and value.strip(): values.append(value.strip())
        return values[0] if len(values)==1 else None
    except (OSError,subprocess.SubprocessError): return None

def laptop_url(port=5000):
    if not 1<=port<=65535: raise ValueError('Invalid HTTP port')
    records=lan_addresses()
    if not records: raise ValueError('No LAN IPv4 address detected; connect the laptop to the test network first')
    for record in records:
        print('LAN:',record['ip'],record['name'], '(virtual; choose only for a confirmed hotspot/bridge)' if record['virtual'] else '')
    host=suggested_host(records)
    value=input('Laptop LAN IPv4'+(' ['+host+']' if host else '')+': ').strip() or host
    if value is None: raise ValueError('Choose the laptop IPv4 address accessible to Board 1')
    value=device_host(value)
    if value not in [record['ip'] for record in records]:
        raise ValueError('That address is not currently assigned to this laptop; reconnect and retry')
    return 'http://'+value+':'+str(port)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--json',action='store_true')
    args=parser.parse_args();records=lan_addresses()
    print(json.dumps(records,ensure_ascii=False,indent=2) if args.json else
          '\n'.join(r['ip']+' '+r['name']+(' (virtual)' if r['virtual'] else '') for r in records))
