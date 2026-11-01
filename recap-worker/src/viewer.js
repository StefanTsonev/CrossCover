// Optional public companion page.
// book text is never served by this page or by the public API.
export const VIEWER = `<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>CrossCover Story So Far</title>
<style>body{font:18px system-ui;max-width:48rem;margin:2rem auto;padding:1rem}input,button{font:inherit;padding:.5rem}input{max-width:100%;box-sizing:border-box}#summary{white-space:pre-wrap}</style>
<h1>Story So Far</h1>
<form><button>View</button></form>
<p id="status" role="status"></p><h2 id="chapter"></h2><p id="summary"></p>
<script>document.querySelector('form').onsubmit=async e=>{e.preventDefault();const status=document.querySelector('#status');const id=location.hash.slice(1);if(!/^[a-f0-9]{64}$/.test(id)){status.textContent='Invalid recap ID';return;}try{const r=await fetch('/v1/recaps/'+id);if(!r.ok)throw Error('Could not load recap ('+r.status+')');const data=await r.json();document.querySelector('#summary').textContent=data.summary||'';document.querySelector('#chapter').textContent=data.chapterTitle||'';status.textContent='AI recap from the completed portion of your EPUB. May contain mistakes.';}catch(error){status.textContent=error.message;}};</script></html>`;
