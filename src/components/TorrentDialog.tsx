import React, { useCallback, useEffect, useState } from 'react'
import { invoke } from '@tauri-apps/api/core'
import { X, Magnet, FileUp, FolderOpen, Pause, Play, Trash2, RefreshCw, ArrowDown, ArrowUp } from 'lucide-react'

type TorrentRow = {
  id: string
  name?: string
  savePath?: string
  progress?: number
  downloaded?: number
  totalWanted?: number
  downloadRate?: number
  uploadRate?: number
  numSeeds?: number
  numPeers?: number
  state?: string
  paused?: boolean
  isFinished?: boolean
  isSeeding?: boolean
}

function bytes(value = 0): string {
  if (value < 1024) return `${value} B`
  const units = ['KB', 'MB', 'GB', 'TB']
  let n = value / 1024
  let i = 0
  while (n >= 1024 && i < units.length - 1) { n /= 1024; i++ }
  return `${n.toFixed(1)} ${units[i]}`
}

function rate(value = 0): string { return `${bytes(value)}/s` }

export function TorrentDialog({ onClose }: { onClose: () => void }) {
  const [uri, setUri] = useState('')
  const [savePath, setSavePath] = useState('')
  const [torrents, setTorrents] = useState<TorrentRow[]>([])
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [message, setMessage] = useState('')

  const refresh = useCallback(async () => {
    const result = await invoke<{ torrents?: TorrentRow[] }>('torrent_list')
    setTorrents(result.torrents || [])
  }, [])

  useEffect(() => {
    invoke<any>('config_get').then(config => setSavePath(config.downloadDir || '')).catch(() => {})
    refresh().catch(e => setError(String(e)))
    const timer = window.setInterval(() => refresh().catch(() => {}), 1500)
    return () => window.clearInterval(timer)
  }, [refresh])

  const addMagnet = async () => {
    const value = uri.trim()
    if (!value.startsWith('magnet:?')) {
      setError('Paste a valid magnet URI.')
      return
    }
    if (!savePath.trim()) {
      setError('Choose a destination folder.')
      return
    }
    setBusy(true); setError(''); setMessage('')
    try {
      await invoke('torrent_add_magnet', { uri: value, savePath: savePath.trim(), paused: false })
      setUri('')
      setMessage('Magnet added to the torrent session.')
      await refresh()
    } catch (e) { setError(String(e)) }
    finally { setBusy(false) }
  }

  const addTorrentFile = async () => {
    if (!savePath.trim()) { setError('Choose a destination folder first.'); return }
    setBusy(true); setError(''); setMessage('')
    try {
      const file = await invoke<string | null>('dialog_select_torrent_file')
      if (!file) return
      await invoke('torrent_add_file', { file, savePath: savePath.trim(), paused: false })
      setMessage('Torrent file added to the session.')
      await refresh()
    } catch (e) { setError(String(e)) }
    finally { setBusy(false) }
  }

  const toggle = async (torrent: TorrentRow) => {
    setError('')
    try {
      await invoke(torrent.paused ? 'torrent_resume' : 'torrent_pause', { id: torrent.id })
      await refresh()
    } catch (e) { setError(String(e)) }
  }

  const remove = async (torrent: TorrentRow) => {
    setError('')
    try {
      await invoke('torrent_remove', { id: torrent.id })
      await refresh()
    } catch (e) { setError(String(e)) }
  }

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 backdrop-blur-sm p-4">
      <div className="w-full max-w-4xl max-h-[88vh] flex flex-col bg-qdm-bg border border-qdm-border rounded-2xl shadow-2xl overflow-hidden">
        <div className="flex items-center justify-between px-5 py-4 border-b border-qdm-border bg-qdm-surface/70">
          <div className="flex items-center gap-3">
            <div className="w-9 h-9 rounded-xl bg-qdm-accent/15 text-qdm-accent flex items-center justify-center"><Magnet size={18} /></div>
            <div>
              <h2 className="text-sm font-semibold text-qdm-text">BitTorrent</h2>
              <p className="text-[11px] text-qdm-textMuted">Powered by libtorrent · {torrents.length} torrents</p>
            </div>
          </div>
          <button onClick={onClose} className="p-2 rounded-lg text-qdm-textMuted hover:text-qdm-text hover:bg-qdm-surfaceHover"><X size={17} /></button>
        </div>

        <div className="p-4 border-b border-qdm-border space-y-3">
          <label className="block text-[11px] text-qdm-textSecondary">Save to</label>
          <div className="flex gap-2">
            <input value={savePath} onChange={e => setSavePath(e.target.value)} className="input-qdm flex-1 min-w-0" placeholder="Destination folder" />
            <button onClick={async () => {
              try { const path = await invoke<string | null>('dialog_select_folder'); if (path) setSavePath(path) } catch (e) { setError(String(e)) }
            }} className="btn-secondary px-3" title="Choose folder"><FolderOpen size={15} /></button>
          </div>
          <div className="flex gap-2">
            <input value={uri} onChange={e => setUri(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') void addMagnet() }} className="input-qdm flex-1 min-w-0" placeholder="magnet:?xt=urn:btih:..." />
            <button disabled={busy} onClick={() => void addMagnet()} className="btn-primary px-4 flex items-center gap-2 disabled:opacity-50"><Magnet size={14} /> Add magnet</button>
            <button disabled={busy} onClick={() => void addTorrentFile()} className="btn-secondary px-3 flex items-center gap-2 disabled:opacity-50"><FileUp size={14} /> .torrent</button>
          </div>
          {error && <p className="text-xs text-qdm-danger break-words">{error}</p>}
          {message && <p className="text-xs text-qdm-success">{message}</p>}
        </div>

        <div className="flex-1 overflow-auto">
          {torrents.length === 0 ? (
            <div className="h-40 flex flex-col items-center justify-center text-qdm-textMuted gap-2">
              <Magnet size={24} />
              <p className="text-sm">No torrents in this session yet</p>
            </div>
          ) : (
            <div className="divide-y divide-qdm-border/70">
              {torrents.map(torrent => (
                <div key={torrent.id} className="px-5 py-3 hover:bg-qdm-surface/40">
                  <div className="flex items-start gap-3">
                    <div className="flex-1 min-w-0">
                      <div className="text-sm text-qdm-text font-medium truncate" title={torrent.name || torrent.id}>{torrent.name || 'Fetching metadata…'}</div>
                      <div className="text-[10px] text-qdm-textMuted mt-1 flex flex-wrap gap-x-3 gap-y-1">
                        <span>{torrent.state || 'queued'}</span>
                        <span>{bytes(torrent.downloaded)} / {bytes(torrent.totalWanted)}</span>
                        <span className="flex items-center gap-1"><ArrowDown size={10} />{rate(torrent.downloadRate)}</span>
                        <span className="flex items-center gap-1"><ArrowUp size={10} />{rate(torrent.uploadRate)}</span>
                        <span>{torrent.numPeers || 0} peers · {torrent.numSeeds || 0} seeds</span>
                      </div>
                      <div className="h-1.5 bg-qdm-border rounded-full overflow-hidden mt-2">
                        <div className="h-full bg-gradient-to-r from-qdm-accent to-cyan-400 transition-all" style={{ width: `${Math.max(0, Math.min(100, torrent.progress || 0))}%` }} />
                      </div>
                      <div className="text-[10px] text-qdm-textMuted mt-1">{(torrent.progress || 0).toFixed(1)}%{torrent.isSeeding ? ' · Seeding' : ''}</div>
                    </div>
                    <div className="flex gap-1">
                      <button onClick={() => void toggle(torrent)} className="p-2 rounded-lg text-qdm-textSecondary hover:text-qdm-accent hover:bg-qdm-accent/10" title={torrent.paused ? 'Resume' : 'Pause'}>
                        {torrent.paused ? <Play size={14} /> : <Pause size={14} />}
                      </button>
                      <button onClick={() => void remove(torrent)} className="p-2 rounded-lg text-qdm-textSecondary hover:text-qdm-danger hover:bg-qdm-danger/10" title="Remove torrent">
                        <Trash2 size={14} />
                      </button>
                    </div>
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>
        <div className="flex items-center justify-between px-5 py-3 border-t border-qdm-border bg-qdm-surface/50">
          <button onClick={() => refresh().catch(e => setError(String(e)))} className="text-xs text-qdm-textSecondary hover:text-qdm-text flex items-center gap-2"><RefreshCw size={12} /> Refresh status</button>
          <button onClick={onClose} className="btn-secondary px-4 py-2 text-xs">Close</button>
        </div>
      </div>
    </div>
  )
}
