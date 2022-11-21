const output = document.getElementById('output')

function appendLine(line, className = '') {
  const div = document.createElement('div')
  div.className = className
  div.textContent = line
  output.appendChild(div)
  output.scrollTop = output.scrollHeight
}

var Module = {
  postRun: [],
  print(...args) {
    const text = args.join(' ')
    appendLine(text)
  },
  printErr(...args) {
    const text = args.join(' ')
    appendLine(text, 'err')
  },
  setStatus(text) { console.log('set status:', text) },
  totalDependencies: 0,
  monitorRunDependencies: function(left) {
    this.totalDependencies = Math.max(this.totalDependencies, left);
    Module.setStatus(left ? 'Preparing... (' + (this.totalDependencies-left) + '/' + this.totalDependencies + ')' : 'All downloads complete.');
  }
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

window.getLineInput = function() {
  return new Promise(resolve => prompters.push(resolve))
}

document.body.addEventListener('click', e => {
  if(e.target.className == 'lxcmd') {
    replInput.value = e.target.textContent
    setTimeout(() => replInput.focus())
    return
  }
})

import createLxModule from './lx.js'
const lx = await createLxModule(Module)
lx._runRepl()
