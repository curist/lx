const output = document.getElementById('output')

function appendLine(line, className = '') {
  const div = document.createElement('div')
  div.className = className
  div.textContent = line
  output.appendChild(div)
  output.scrollTop = output.scrollHeight
}

var Module = {
  print(...args) { appendLine(args.join(' ')) },
  printErr(...args) { appendLine(args.join(' '), 'err') },
  setStatus(text) { console.log('set status:', text) },
};
window.onerror = function() {
  Module.setStatus('Exception thrown, see JavaScript console');
  Module.setStatus = function(text) {
    if (text) console.error('[post-exception status] ' + text);
  };
};

const replInputForm = document.getElementById('repl-input-form')
const replInput = document.getElementById('repl-input')
const prompters = []
replInputForm.addEventListener('submit', e => {
  e.preventDefault()
  const line = replInput.value
  appendLine(line, 'lxcmd')
  replInput.value = ''
  const prompter = prompters.shift()
  if (prompter) { prompter(line) }
})

window.getLineInput = () => new Promise(resolve => prompters.push(resolve))

document.body.addEventListener('click', e => {
  if(e.target.className == 'lxcmd') {
    replInput.value = e.target.textContent
    return setTimeout(() => replInput.focus())
  }
})

import createLxModule from './lx.js'
const lx = await createLxModule(Module)
lx._runRepl()
