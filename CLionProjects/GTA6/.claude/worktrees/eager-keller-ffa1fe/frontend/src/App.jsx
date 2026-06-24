import { useEffect, useMemo, useRef, useState } from 'react'

const API = import.meta.env.VITE_API_URL || 'http://localhost:8080'
const ROLES = ['Developer', 'Manager', 'Designer', 'DevOps Engineer', 'Product Owner']

const initials = (name) =>
  name
    .trim()
    .split(/\s+/)
    .map((part) => part[0])
    .slice(0, 2)
    .join('')
    .toUpperCase()

const pad = (n, width = 3) => String(n).padStart(width, '0')

export default function App() {
  const [employees, setEmployees] = useState([])
  const [form, setForm] = useState({ name: '', role: 'Developer', salary: '' })
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [formError, setFormError] = useState('')
  const [query, setQuery] = useState('')
  const nameRef = useRef(null)
  const searchRef = useRef(null)

  const load = async () => {
    try {
      setLoading(true)
      const res = await fetch(`${API}/api/employees`)
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      setEmployees(await res.json())
      setError('')
    } catch {
      setError("Impossible de joindre l'API. Le backend tourne ?")
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    load()
  }, [])

  useEffect(() => {
    const onKeyDown = (event) => {
      if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'k') {
        event.preventDefault()
        searchRef.current?.focus()
      }
    }

    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [])

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase()
    if (!q) return employees

    return employees.filter((employee) => {
      const salary = employee.salary == null ? '' : String(employee.salary)
      return (
        employee.name.toLowerCase().includes(q) ||
        employee.role.toLowerCase().includes(q) ||
        salary.includes(q)
      )
    })
  }, [employees, query])

  const byRole = useMemo(() => {
    const map = {}
    for (const employee of employees) {
      map[employee.role] = (map[employee.role] || 0) + 1
    }
    return map
  }, [employees])

  const topRole = useMemo(() => {
    const sorted = Object.entries(byRole).sort((a, b) => b[1] - a[1])
    return sorted[0] || ['-', 0]
  }, [byRole])

  const valid = form.name.trim() && form.role.trim()

  const add = async (event) => {
    event.preventDefault()
    setFormError('')

    if (!form.name.trim()) {
      setFormError('Name is required')
      return
    }

    const salaryValue = form.salary.trim()
    if (salaryValue && Number.isNaN(Number(salaryValue))) {
      setFormError('Salary must be a valid number')
      return
    }

    const res = await fetch(`${API}/api/employees`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        name: form.name.trim(),
        role: form.role,
        salary: salaryValue ? Number(salaryValue) : null,
      }),
    })

    if (!res.ok) {
      setFormError('Create failed, check backend logs')
      return
    }

    setForm({ name: '', role: form.role, salary: '' })
    nameRef.current?.focus()
    await load()
  }

  const remove = async (id) => {
    const res = await fetch(`${API}/api/employees/${id}`, { method: 'DELETE' })
    if (res.ok) await load()
  }

  return (
    <>
      <div className="bgwrap" aria-hidden="true">
        <div className="orb a"></div>
        <div className="orb b"></div>
        <div className="orb c"></div>
      </div>
      <div className="grain" aria-hidden="true"></div>
      <div className="vignette" aria-hidden="true"></div>

      <div className="shell" data-screen-label="01 Employees">
        <header className="topbar">
          <div className="brand">
            <div className="logo" aria-hidden="true"></div>
            <div className="word">
              Nexus<span className="muted-word">/HR</span>
              <small>Employees - DevOps TP</small>
            </div>
          </div>
          <div className="meta">
            <span className="hide-mobile">
              <span className="dot"></span>Live - API
            </span>
            <span className="hide-mobile">v2.0.0</span>
            <span>
              {new Date()
                .toLocaleDateString('en-GB', { day: '2-digit', month: 'short', year: 'numeric' })
                .toUpperCase()}
            </span>
          </div>
        </header>

        <section className="hero">
          <div>
            <div className="eyebrow">Workforce / Console</div>
            <h1>
              People, <em>in motion.</em>
            </h1>
            <p className="lede">
              Employee registry powered by the Spring backend. Add, list, filter, and delete members
              from one interface.
            </p>
          </div>
          <div className="stats" role="group" aria-label="Workforce snapshot">
            <div className="stat">
              <div className="k">Headcount</div>
              <div className="v">
                {employees.length}
                <small>FTE</small>
              </div>
              <div className="delta">live data</div>
            </div>
            <div className="stat">
              <div className="k">Roles</div>
              <div className="v">
                {Object.keys(byRole).length}
                <small>ACTIVE</small>
              </div>
              <div className="delta">stable</div>
            </div>
            <div className="stat">
              <div className="k">Top role</div>
              <div className="v top-role">{topRole[0]}</div>
              <div className="delta">{topRole[1]} members</div>
            </div>
          </div>
        </section>

        <div className="seclabel">
          <div className="l">
            <span className="tag">01</span>
            <h2>Onboard a new member</h2>
          </div>
          <div className="r">Form - API create</div>
        </div>

        <form className="panel" onSubmit={add} noValidate>
          <div className="form">
            <Field
              idx="01"
              label="Full name"
              name="name"
              placeholder="e.g. Camille Roux"
              value={form.name}
              onChange={(v) => setForm((f) => ({ ...f, name: v }))}
              inputRef={nameRef}
            />
            <div className="field">
              <label htmlFor="role">
                <span className="idx">02</span> Role
              </label>
              <div className="inputWrap">
                <select
                  id="role"
                  value={form.role}
                  onChange={(e) => setForm((f) => ({ ...f, role: e.target.value }))}
                >
                  {ROLES.map((role) => (
                    <option key={role} value={role}>
                      {role}
                    </option>
                  ))}
                </select>
                <span className="underline"></span>
              </div>
            </div>
            <Field
              idx="03"
              label="Salary"
              name="salary"
              type="number"
              placeholder="e.g. 54000"
              value={form.salary}
              onChange={(v) => setForm((f) => ({ ...f, salary: v }))}
            />
            <div className="submit-wrap">
              <button className="submit" type="submit" disabled={!valid}>
                Add employee <span className="arrow">-&gt;</span>
              </button>
            </div>
          </div>
          {formError && <div className="formerror">{formError}</div>}
        </form>

        <div className="roster-bar">
          <div className="l">
            <h2>Roster</h2>
            <span className="count">
              [ {pad(filtered.length)} / {pad(employees.length)} ]
            </span>
          </div>
          <label className="search">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <circle cx="11" cy="11" r="7" />
              <path d="m20 20-3.5-3.5" />
            </svg>
            <input
              ref={searchRef}
              placeholder="Filter by name, role, salary..."
              value={query}
              onChange={(e) => setQuery(e.target.value)}
            />
            <kbd>Cmd+K</kbd>
          </label>
        </div>

        <div className="grid">
          {loading && (
            <div className="empty">
              <div className="big">Loading employees...</div>
              <div className="sub">Fetching from backend API</div>
            </div>
          )}

          {!loading && error && (
            <div className="empty">
              <div className="big">API unavailable</div>
              <div className="sub">{error}</div>
            </div>
          )}

          {!loading && !error && filtered.length === 0 && (
            <div className="empty">
              <div className="big">No matching members</div>
              <div className="sub">Try another filter or add someone above</div>
            </div>
          )}

          {!loading &&
            !error &&
            filtered.map((employee, index) => (
              <article
                className="card"
                key={employee.id}
                style={{ animationDelay: `${Math.min(index * 40, 400)}ms` }}
              >
                <div className="row1">
                  <span className="id">ID - {employee.id}</span>
                  <button
                    className="delete"
                    type="button"
                    aria-label={`Remove ${employee.name}`}
                    onClick={() => remove(employee.id)}
                  >
                    x
                  </button>
                </div>

                <div className="person">
                  <div className="avatar">
                    <span>{initials(employee.name)}</span>
                  </div>
                  <div className="person-meta">
                    <div className="name">{employee.name}</div>
                    <div className="role">{employee.role}</div>
                  </div>
                </div>

                <div className="stats-mini">
                  <div>
                    <span className="k">Role</span>
                    <span className="v">{employee.role}</span>
                  </div>
                  <div>
                    <span className="k">Salary</span>
                    <span className="v">
                      <span className="ddot"></span>
                      {employee.salary == null ? '-' : `${employee.salary} EUR`}
                    </span>
                  </div>
                </div>
              </article>
            ))}
        </div>

        <footer className="foot">
          <span>DevOps TP - Employees Frontend</span>
          <span className="gradline"></span>
          <span>
            Build {pad(Math.floor(Math.random() * 9000) + 1000, 4)} - {new Date().getFullYear()}
          </span>
        </footer>
      </div>
    </>
  )
}

function Field({ idx, label, name, value, onChange, type = 'text', placeholder, inputRef }) {
  return (
    <div className="field">
      <label htmlFor={name}>
        <span className="idx">{idx}</span> {label}
      </label>
      <div className="inputWrap">
        <input
          ref={inputRef}
          id={name}
          name={name}
          type={type}
          value={value}
          placeholder={placeholder}
          onChange={(e) => onChange(e.target.value)}
          autoComplete="off"
        />
        <span className="underline"></span>
      </div>
    </div>
  )
}
