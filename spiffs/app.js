function addCard() {
 const id = document.getElementById('cardId').value;
 fetch('/api/card', {method:'PUT', body: JSON.stringify({id})});
}

const ws = new WebSocket(`ws://${location.host}/ws`);
ws.onmessage = (e)=>{
 const data = JSON.parse(e.data);
 const li = document.createElement('li');
 li.innerText = data.card + ' ' + (data.ok?'OK':'DENIED');
 document.getElementById('live').appendChild(li);
};
