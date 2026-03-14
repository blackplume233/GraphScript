import { Component, type ReactNode } from 'react'

interface Props {
  children: ReactNode
  fallback?: ReactNode
}

interface State {
  hasError: boolean
  error: Error | null
}

export class ErrorBoundary extends Component<Props, State> {
  state: State = { hasError: false, error: null }

  static getDerivedStateFromError(error: Error): State {
    return { hasError: true, error }
  }

  render() {
    if (this.state.hasError) {
      if (this.props.fallback) return this.props.fallback

      return (
        <div className="flex flex-col items-center justify-center h-full gap-3 p-8">
          <div className="w-10 h-10 rounded-full bg-destructive/10 flex items-center justify-center">
            <div className="w-3 h-3 rounded-full bg-destructive" />
          </div>
          <div className="text-sm text-foreground/70 font-medium">Canvas Error</div>
          <div className="text-[11px] text-muted-foreground/50 text-center max-w-[300px] font-mono">
            {this.state.error?.message ?? 'Unknown error'}
          </div>
          <button
            className="mt-2 px-3 py-1.5 text-xs rounded bg-secondary hover:bg-secondary/80 transition-colors"
            onClick={() => this.setState({ hasError: false, error: null })}
          >
            Retry
          </button>
        </div>
      )
    }

    return this.props.children
  }
}
